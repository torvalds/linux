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

//============================================================
// include files
//============================================================

#include "mp_precomp.h"
#include "phydm_precomp.h"

VOID
PHYDM_InitDebugSetting(
	IN		PDM_ODM_T		pDM_Odm
)
{
	pDM_Odm->DebugLevel = ODM_DBG_TRACE;
	
	pDM_Odm->fw_DebugComponents = 0;
	pDM_Odm->DebugComponents			=
		\
#if DBG
/*BB Functions*/
/*									ODM_COMP_DIG					|*/
/*									ODM_COMP_RA_MASK				|*/
/*									ODM_COMP_DYNAMIC_TXPWR			|*/
/*									ODM_COMP_FA_CNT				|*/
/*									ODM_COMP_RSSI_MONITOR			|*/
/*									ODM_COMP_SNIFFER				|*/
/*									ODM_COMP_ANT_DIV				|*/
/*									ODM_COMP_NOISY_DETECT			|*/
/*									ODM_COMP_RATE_ADAPTIVE			|*/
/*									ODM_COMP_PATH_DIV				|*/
/*									ODM_COMP_DYNAMIC_PRICCA		|*/
/*									ODM_COMP_MP					|*/
/*									ODM_COMP_CFO_TRACKING			|*/
/*									ODM_COMP_ACS					|*/
/*									PHYDM_COMP_ADAPTIVITY			|*/
/*									PHYDM_COMP_RA_DBG				|*/
/*									PHYDM_COMP_TXBF					|*/

/*MAC Functions*/
/*									ODM_COMP_EDCA_TURBO			|*/
/*									ODM_FW_DEBUG_TRACE				|*/

/*RF Functions*/
/*									ODM_COMP_TX_PWR_TRACK			|*/
/*									ODM_COMP_CALIBRATION			|*/

/*Common*/
/*									ODM_PHY_CONFIG					|*/
/*									ODM_COMP_INIT					|*/
/*									ODM_COMP_COMMON				|*/
/*									ODM_COMP_API				|*/


#endif
		0;

	pDM_Odm->fw_buff_is_enpty = TRUE;
	pDM_Odm->pre_c2h_seq = 0;
}

VOID
phydm_BB_RxHang_Info(
	IN		PVOID			pDM_VOID,
	IN		u4Byte			*_used,
	OUT		char				*output,
	IN		u4Byte			*_out_len
)
{
	u4Byte	value32 = 0;
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte used = *_used;
	u4Byte out_len = *_out_len;

	if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		return;

	value32 = ODM_GetBBReg(pDM_Odm, 0xF80 , bMaskDWord);
	PHYDM_SNPRINTF((output + used, out_len - used,  "\r\n %-35s = 0x%x", "rptreg of sc/bw/ht/...", value32));

	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		ODM_SetBBReg(pDM_Odm, 0x198c , BIT2|BIT1|BIT0, 7);

	/* dbg_port = basic state machine */
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x000);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "basic state machine", value32));
	}

	/* dbg_port = state machine */
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x007);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "state machine", value32));
	}

	/* dbg_port = CCA-related*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x204);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "CCA-related", value32));
	}


	/* dbg_port = edcca/rxd*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x278);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "edcca/rxd", value32));
	}

	/* dbg_port = rx_state/mux_state/ADC_MASK_OFDM*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x290);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx_state/mux_state/ADC_MASK_OFDM", value32));
	}

	/* dbg_port = bf-related*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x2B2);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "bf-related", value32));
	}

	/* dbg_port = bf-related*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x2B8);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "bf-related", value32));
	}

	/* dbg_port = txon/rxd*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xA03);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "txon/rxd", value32));
	}

	/* dbg_port = l_rate/l_length*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xA0B);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "l_rate/l_length", value32));
	}

	/* dbg_port = rxd/rxd_hit*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xA0D);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rxd/rxd_hit", value32));
	}

	/* dbg_port = dis_cca*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAA0);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "dis_cca", value32));
	}


	/* dbg_port = tx*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAB0);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "tx", value32));
	}

	/* dbg_port = rx plcp*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD0);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));

		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD1);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));

		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD2);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));

		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD3);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));
	}

}

VOID
phydm_BB_Debug_Info(
	IN		PVOID			pDM_VOID,
	IN		u4Byte			*_used,
	OUT		char				*output,
	IN		u4Byte			*_out_len
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte used = *_used;
	u4Byte out_len = *_out_len;
	
	char *tmp_string = NULL;

	u1Byte	RX_HT_BW, RX_VHT_BW, RXSC, RX_HT, RX_BW;
	static u1Byte vRX_BW ;
	u4Byte	value32, value32_1, value32_2, value32_3;
	s4Byte	SFO_A, SFO_B, SFO_C, SFO_D;
	s4Byte	LFO_A, LFO_B, LFO_C, LFO_D;
	static u1Byte	MCSS, Tail, Parity, rsv, vrsv, idx, smooth, htsound, agg, stbc, vstbc, fec, fecext, sgi, sgiext, htltf, vgid, vNsts, vtxops, vrsv2, vbrsv, bf, vbcrc;
	static u2Byte	HLength, htcrc8, Length;
	static u2Byte vpaid;
	static u2Byte	vLength, vhtcrc8, vMCSS, vTail, vbTail;
	static u1Byte	HMCSS, HRX_BW;

	u1Byte    pwDB;
	s1Byte    RXEVM_0, RXEVM_1, RXEVM_2 ;
	u1Byte    RF_gain_pathA, RF_gain_pathB, RF_gain_pathC, RF_gain_pathD;
	u1Byte    RX_SNR_pathA, RX_SNR_pathB, RX_SNR_pathC, RX_SNR_pathD;
	s4Byte    sig_power;

	const char *L_rate[8] = {"6M", "9M", "12M", "18M", "24M", "36M", "48M", "54M"};

	/*
	const double evm_comp_20M = 0.579919469776867; //10*log10(64.0/56.0)
	const double evm_comp_40M = 0.503051183113957; //10*log10(128.0/114.0)
	const double evm_comp_80M = 0.244245993314183; //10*log10(256.0/242.0)
	const double evm_comp_160M = 0.244245993314183; //10*log10(512.0/484.0)
	   */

	if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		return;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s\n", "BB Report Info"));

	/*BW & Mode Detection*/
	
	value32 = ODM_GetBBReg(pDM_Odm, 0xf80 , bMaskDWord);
	value32_2 = value32;
	RX_HT_BW = (u1Byte)(value32 & 0x1);
	RX_VHT_BW = (u1Byte)((value32 >> 1) & 0x3);
	RXSC = (u1Byte)(value32 & 0x78);
	value32_1 = (value32 & 0x180) >> 7;
	RX_HT = (u1Byte)(value32_1);

	RX_BW = 0;

	if (RX_HT == 2) {
		if (RX_VHT_BW == 0) {
			tmp_string = "20M";
		} else if (RX_VHT_BW == 1) {
			tmp_string = "40M";
		} else {
			tmp_string = "80M";
		}
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s %s %s", "Mode", "VHT", tmp_string));
		RX_BW = RX_VHT_BW;
	} else if (RX_HT == 1) {
		if (RX_HT_BW == 0) {
			tmp_string = "20M";
		} else if (RX_HT_BW == 1) {
			tmp_string = "40M";
		}
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s %s %s", "Mode", "HT", tmp_string));
		RX_BW = RX_HT_BW;
	} else {
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s %s", "Mode", "Legacy"));
	}

	if (RX_HT != 0) {
		if (RXSC == 0)
			tmp_string = "duplicate/full bw";
		else if (RXSC == 1)
			tmp_string = "usc20-1";
		else if (RXSC == 2)
			tmp_string = "lsc20-1";
		else if (RXSC == 3)
			tmp_string = "usc20-2";
		else if (RXSC == 4)
			tmp_string = "lsc20-2";
		else if (RXSC == 9)
			tmp_string = "usc40";
		else if (RXSC == 10)
			tmp_string = "lsc40";
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s", tmp_string));
	}

	/* RX signal power and AGC related info*/

	value32 = ODM_GetBBReg(pDM_Odm, 0xF90 , bMaskDWord);
	pwDB = (u1Byte)((value32 & bMaskByte1) >> 8);
	pwDB = pwDB >> 1;
	sig_power = -110 + pwDB;
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "OFDM RX Signal Power(dB)", sig_power));

	value32 = ODM_GetBBReg(pDM_Odm, 0xd14 , bMaskDWord);
	RX_SNR_pathA = (u1Byte)(value32 & 0xFF) >> 1;
	RF_gain_pathA = (s1Byte)((value32 & bMaskByte1) >> 8);
	RF_gain_pathA *= 2;
	value32 = ODM_GetBBReg(pDM_Odm, 0xd54 , bMaskDWord);
	RX_SNR_pathB = (u1Byte)(value32 & 0xFF) >> 1;
	RF_gain_pathB = (s1Byte)((value32 & bMaskByte1) >> 8);
	RF_gain_pathB *= 2;
	value32 = ODM_GetBBReg(pDM_Odm, 0xd94 , bMaskDWord);
	RX_SNR_pathC = (u1Byte)(value32 & 0xFF) >> 1;
	RF_gain_pathC = (s1Byte)((value32 & bMaskByte1) >> 8);
	RF_gain_pathC *= 2;
	value32 = ODM_GetBBReg(pDM_Odm, 0xdd4 , bMaskDWord);
	RX_SNR_pathD = (u1Byte)(value32 & 0xFF) >> 1;
	RF_gain_pathD = (s1Byte)((value32 & bMaskByte1) >> 8);
	RF_gain_pathD *= 2;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d / %d", "OFDM RX RF Gain(A/B/C/D)", RF_gain_pathA, RF_gain_pathB, RF_gain_pathC, RF_gain_pathD));


	/* RX Counter related info*/

	value32 = ODM_GetBBReg(pDM_Odm, 0xF08, bMaskDWord);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "OFDM CCA Counter", ((value32&0xFFFF0000)>>16)));
	
	value32 = ODM_GetBBReg(pDM_Odm, 0xFD0, bMaskDWord);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "OFDM SBD Fail Counter", value32&0xFFFF));

	value32 = ODM_GetBBReg(pDM_Odm, 0xFC4, bMaskDWord);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "VHT SIGA/SIGB CRC8 Fail Counter", value32&0xFFFF, ((value32&0xFFFF0000)>>16)));

	value32 = ODM_GetBBReg(pDM_Odm, 0xFCC, bMaskDWord);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "CCK CCA Counter", value32&0xFFFF));

	value32 = ODM_GetBBReg(pDM_Odm, 0xFBC, bMaskDWord);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "LSIG (Parity Fail/Rate Illegal) Counter", value32&0xFFFF, ((value32&0xFFFF0000)>>16)));

	value32_1 = ODM_GetBBReg(pDM_Odm, 0xFC8, bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xFC0, bMaskDWord);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "HT/VHT MCS NOT SUPPORT counter", ((value32_2&0xFFFF0000)>>16), value32_1&0xFFFF));

	/* PostFFT related info*/
	value32 = ODM_GetBBReg(pDM_Odm, 0xF8c , bMaskDWord);
	RXEVM_0 = (s1Byte)((value32 & bMaskByte2) >> 16);
	RXEVM_0 /= 2;
	if (RXEVM_0 < -63)
		RXEVM_0 = 0;

	RXEVM_1 = (s1Byte)((value32 & bMaskByte3) >> 24);
	RXEVM_1 /= 2;
	value32 = ODM_GetBBReg(pDM_Odm, 0xF88 , bMaskDWord);
	RXEVM_2 = (s1Byte)((value32 & bMaskByte2) >> 16);
	RXEVM_2 /= 2;

	if (RXEVM_1 < -63)
		RXEVM_1 = 0;
	if (RXEVM_2 < -63)
		RXEVM_2 = 0;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d", "RXEVM (1ss/2ss/3ss)", RXEVM_0, RXEVM_1, RXEVM_2));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D, dB)", RX_SNR_pathA, RX_SNR_pathB, RX_SNR_pathC, RX_SNR_pathD));

	value32 = ODM_GetBBReg(pDM_Odm, 0xF8C , bMaskDWord);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "CSI_1st /CSI_2nd", value32&0xFFFF, ((value32&0xFFFF0000)>>16)));

	/*BW & Mode Detection*/

	/*Reset Page F Counter*/
	ODM_SetBBReg(pDM_Odm, 0xB58 , BIT0, 1);
	ODM_SetBBReg(pDM_Odm, 0xB58 , BIT0, 0);

	/*CFO Report Info*/
	/*Short CFO*/
	value32 = ODM_GetBBReg(pDM_Odm, 0xd0c , bMaskDWord);
	value32_1 = ODM_GetBBReg(pDM_Odm, 0xd4c , bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xd8c , bMaskDWord);
	value32_3 = ODM_GetBBReg(pDM_Odm, 0xdcc , bMaskDWord);

	SFO_A = (s4Byte)(value32 & 0xfff);
	SFO_B = (s4Byte)(value32_1 & 0xfff);
	SFO_C = (s4Byte)(value32_2 & 0xfff);
	SFO_D = (s4Byte)(value32_3 & 0xfff);

	LFO_A = (s4Byte)(value32 >> 16);
	LFO_B = (s4Byte)(value32_1 >> 16);
	LFO_C = (s4Byte)(value32_2 >> 16);
	LFO_D = (s4Byte)(value32_3 >> 16);

	/*SFO 2's to dec*/
	if (SFO_A > 2047)
		SFO_A = SFO_A - 4096;
	SFO_A = (SFO_A * 312500) / 2048;
	if (SFO_B > 2047)
		SFO_B = SFO_B - 4096;
	SFO_B = (SFO_B * 312500) / 2048;
	if (SFO_C > 2047)
		SFO_C = SFO_C - 4096;
	SFO_C = (SFO_C * 312500) / 2048;
	if (SFO_D > 2047)
		SFO_D = SFO_D - 4096;
	SFO_D = (SFO_D * 312500) / 2048;

	/*LFO 2's to dec*/

	if (LFO_A > 4095)
		LFO_A = LFO_A - 8192;

	if (LFO_B > 4095)
		LFO_B = LFO_B - 8192;

	if (LFO_C > 4095)
		LFO_C = LFO_C - 8192;

	if (LFO_D > 4095)
		LFO_D = LFO_D - 8192;
	LFO_A = LFO_A * 312500 / 4096;
	LFO_B = LFO_B * 312500 / 4096;
	LFO_C = LFO_C * 312500 / 4096;
	LFO_D = LFO_D * 312500 / 4096;
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "CFO Report Info"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d /%d", "Short CFO(Hz) <A/B/C/D>", SFO_A, SFO_B, SFO_C, SFO_D));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d /%d", "Long CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D));

	/*SCFO*/
	value32 = ODM_GetBBReg(pDM_Odm, 0xd10 , bMaskDWord);
	value32_1 = ODM_GetBBReg(pDM_Odm, 0xd50 , bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xd90 , bMaskDWord);
	value32_3 = ODM_GetBBReg(pDM_Odm, 0xdd0 , bMaskDWord);

	SFO_A = (s4Byte)(value32 & 0x7ff);
	SFO_B = (s4Byte)(value32_1 & 0x7ff);
	SFO_C = (s4Byte)(value32_2 & 0x7ff);
	SFO_D = (s4Byte)(value32_3 & 0x7ff);

	if (SFO_A > 1023)
		SFO_A = SFO_A - 2048;

	if (SFO_B > 2047)
		SFO_B = SFO_B - 4096;

	if (SFO_C > 2047)
		SFO_C = SFO_C - 4096;

	if (SFO_D > 2047)
		SFO_D = SFO_D - 4096;

	SFO_A = SFO_A * 312500 / 1024;
	SFO_B = SFO_B * 312500 / 1024;
	SFO_C = SFO_C * 312500 / 1024;
	SFO_D = SFO_D * 312500 / 1024;

	LFO_A = (s4Byte)(value32 >> 16);
	LFO_B = (s4Byte)(value32_1 >> 16);
	LFO_C = (s4Byte)(value32_2 >> 16);
	LFO_D = (s4Byte)(value32_3 >> 16);

	if (LFO_A > 4095)
		LFO_A = LFO_A - 8192;

	if (LFO_B > 4095)
		LFO_B = LFO_B - 8192;

	if (LFO_C > 4095)
		LFO_C = LFO_C - 8192;

	if (LFO_D > 4095)
		LFO_D = LFO_D - 8192;
	LFO_A = LFO_A * 312500 / 4096;
	LFO_B = LFO_B * 312500 / 4096;
	LFO_C = LFO_C * 312500 / 4096;
	LFO_D = LFO_D * 312500 / 4096;
	
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d /%d", "Value SCFO(Hz) <A/B/C/D>", SFO_A, SFO_B, SFO_C, SFO_D));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d /%d", "ACQ CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D));

	value32 = ODM_GetBBReg(pDM_Odm, 0xd14 , bMaskDWord);
	value32_1 = ODM_GetBBReg(pDM_Odm, 0xd54 , bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xd94 , bMaskDWord);
	value32_3 = ODM_GetBBReg(pDM_Odm, 0xdd4 , bMaskDWord);

	LFO_A = (s4Byte)(value32 >> 16);
	LFO_B = (s4Byte)(value32_1 >> 16);
	LFO_C = (s4Byte)(value32_2 >> 16);
	LFO_D = (s4Byte)(value32_3 >> 16);

	if (LFO_A > 4095)
		LFO_A = LFO_A - 8192;

	if (LFO_B > 4095)
		LFO_B = LFO_B - 8192;

	if (LFO_C > 4095)
		LFO_C = LFO_C - 8192;

	if (LFO_D > 4095)
		LFO_D = LFO_D - 8192;

	LFO_A = LFO_A * 312500 / 4096;
	LFO_B = LFO_B * 312500 / 4096;
	LFO_C = LFO_C * 312500 / 4096;
	LFO_D = LFO_D * 312500 / 4096;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d /%d", "End CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D));

	value32 = ODM_GetBBReg(pDM_Odm, 0xf20 , bMaskDWord);  /*L SIG*/

	Tail = (u1Byte)((value32 & 0xfc0000) >> 16);
	Parity = (u1Byte)((value32 & 0x20000) >> 16);
	Length = (u2Byte)((value32 & 0x1ffe00) >> 8);
	rsv = (u1Byte)(value32 & 0x10);
	MCSS = (u1Byte)(value32 & 0x0f);

	switch (MCSS) {
	case 0x0b:
		idx = 0;
		break;
	case 0x0f:
		idx = 1;
		break;
	case 0x0a:
		idx = 2;
		break;
	case 0x0e:
		idx = 3;
		break;
	case 0x09:
		idx = 4;
		break;
	case 0x08:
		idx = 5;
		break;
	case 0x0c:
		idx = 6;
		break;
	default:
		idx = 6;
		break;

	}

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "L-SIG"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s : %s", "Rate", L_rate[idx]));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x", "Rsv/Length/Parity", rsv, RX_BW, Length));

	value32 = ODM_GetBBReg(pDM_Odm, 0xf2c , bMaskDWord);  /*HT SIG*/
	if (RX_HT == 1) {

		HMCSS = (u1Byte)(value32 & 0x7F);
		HRX_BW = (u1Byte)(value32 & 0x80);
		HLength = (u2Byte)((value32 >> 8) & 0xffff);
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "HT-SIG1"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x", "MCS/BW/Length", HMCSS, HRX_BW, HLength));

	value32 = ODM_GetBBReg(pDM_Odm, 0xf30 , bMaskDWord);  /*HT SIG*/

	if (RX_HT == 1) {
		smooth = (u1Byte)(value32 & 0x01);
		htsound = (u1Byte)(value32 & 0x02);
		rsv = (u1Byte)(value32 & 0x04);
		agg = (u1Byte)(value32 & 0x08);
		stbc = (u1Byte)(value32 & 0x30);
		fec = (u1Byte)(value32 & 0x40);
		sgi = (u1Byte)(value32 & 0x80);
		htltf = (u1Byte)((value32 & 0x300) >> 8);
		htcrc8 = (u2Byte)((value32 & 0x3fc00) >> 8);
		Tail = (u1Byte)((value32 & 0xfc0000) >> 16);
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "HT-SIG2"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x / %x / %x", "Smooth/NoSound/Rsv/Aggregate/STBC/LDPC", smooth, htsound, rsv, agg, stbc, fec));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x", "SGI/E-HT-LTFs/CRC/Tail", sgi, htltf, htcrc8, Tail));

	value32 = ODM_GetBBReg(pDM_Odm, 0xf2c , bMaskDWord);  /*VHT SIG A1*/
	if (RX_HT == 2) {
		/* value32 = ODM_GetBBReg(pDM_Odm, 0xf2c ,bMaskDWord);*/
		vRX_BW = (u1Byte)(value32 & 0x03);
		vrsv = (u1Byte)(value32 & 0x04);
		vstbc = (u1Byte)(value32 & 0x08);
		vgid = (u1Byte)((value32 & 0x3f0) >> 4);
		vNsts = (u1Byte)(((value32 & 0x1c00) >> 8) + 1);
		vpaid = (u2Byte)(value32 & 0x3fe);
		vtxops = (u1Byte)((value32 & 0x400000) >> 20);
		vrsv2 = (u1Byte)((value32 & 0x800000) >> 20);
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "VHT-SIG-A1"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x / %x / %x / %x / %x", "BW/Rsv1/STBC/GID/Nsts/PAID/TXOPPS/Rsv2", vRX_BW, vrsv, vstbc, vgid, vNsts, vpaid, vtxops, vrsv2));

	value32 = ODM_GetBBReg(pDM_Odm, 0xf30 , bMaskDWord);  /*VHT SIG*/

	if (RX_HT == 2) {
		/*value32 = ODM_GetBBReg(pDM_Odm, 0xf30 ,bMaskDWord); */  /*VHT SIG*/

		//sgi=(u1Byte)(value32&0x01);
		sgiext = (u1Byte)(value32 & 0x03);
		//fec = (u1Byte)(value32&0x04);
		fecext = (u1Byte)(value32 & 0x0C);

		vMCSS = (u1Byte)(value32 & 0xf0);
		bf = (u1Byte)((value32 & 0x100) >> 8);
		vrsv = (u1Byte)((value32 & 0x200) >> 8);
		vhtcrc8 = (u2Byte)((value32 & 0x3fc00) >> 8);
		vTail = (u1Byte)((value32 & 0xfc0000) >> 16);
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "VHT-SIG-A2"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x / %x / %x / %x", "SGI/FEC/MCS/BF/Rsv/CRC/Tail", sgiext, fecext, vMCSS, bf, vrsv, vhtcrc8, vTail));
	
	value32 = ODM_GetBBReg(pDM_Odm, 0xf34 , bMaskDWord);  /*VHT SIG*/
	{
		vLength = (u2Byte)(value32 & 0x1fffff);
		vbrsv = (u1Byte)((value32 & 0x600000) >> 20);
		vbTail = (u2Byte)((value32 & 0x1f800000) >> 20);
		vbcrc = (u1Byte)((value32 & 0x80000000) >> 28);

	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "VHT-SIG-B"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x", "Length/Rsv/Tail/CRC", vLength, vbrsv, vbTail, vbcrc));

	/*for Condition number*/
	if (pDM_Odm->SupportICType & ODM_RTL8822B) {
		s4Byte	condition_num = 0;
		char *factor = NULL;
		
		ODM_SetBBReg(pDM_Odm, 0x1988 , BIT22, 0x1);	/*enable report condition number*/

		condition_num = ODM_GetBBReg(pDM_Odm, 0xf84, bMaskDWord);
		condition_num = (condition_num & 0x3ffff) >> 4;

		if (*pDM_Odm->pBandWidth == ODM_BW80M)
			factor = "256/234";
		else if (*pDM_Odm->pBandWidth == ODM_BW40M)
			factor = "128/108";
		else if (*pDM_Odm->pBandWidth == ODM_BW20M) {
			if (RX_HT != 2 || RX_HT != 1)
				factor = "64/52";	/*HT or VHT*/
			else
				factor = "64/48";	/*legacy*/
		}

		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d (factor = %s)", "Condition Number", condition_num, factor));

	}

}
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
void phydm_sbd_check(
	IN	PDM_ODM_T					pDM_Odm
)
{
	static u4Byte	pkt_cnt = 0;
	static BOOLEAN sbd_state = 0;
	u4Byte	sym_count, count, value32;

	if (sbd_state == 0) {
		pkt_cnt++;
		if (pkt_cnt % 5 == 0) { /*read SBD conter once every 5 packets*/
			ODM_SetTimer(pDM_Odm, &pDM_Odm->sbdcnt_timer, 0); /*ms*/
			sbd_state = 1;
		}
	} else { /*read counter*/
		value32 = ODM_GetBBReg(pDM_Odm, 0xF98, bMaskDWord);
		sym_count = (value32 & 0x7C000000) >> 26;
		count = (value32 & 0x3F00000) >> 20;
		DbgPrint("#SBD#    sym_count   %d   count   %d\n", sym_count, count);
		sbd_state = 0;
	}
}

void phydm_sbd_callback(
	PRT_TIMER		pTimer
)
{
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

#if USE_WORKITEM
	ODM_ScheduleWorkItem(&pDM_Odm->sbdcnt_workitem);
#else
	phydm_sbd_check(pDM_Odm);
#endif
}

void phydm_sbd_workitem_callback(
	IN PVOID            pContext
)
{
	PADAPTER	pAdapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	phydm_sbd_check(pDM_Odm);
}
#endif
VOID
phydm_BasicDbgMessage
(
	IN		PVOID			pDM_VOID
)
{
#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = (PFALSE_ALARM_STATISTICS)PhyDM_Get_Structure(pDM_Odm , PHYDM_FALSEALMCNT);
	PCFO_TRACKING				pCfoTrack = (PCFO_TRACKING)PhyDM_Get_Structure( pDM_Odm, PHYDM_CFOTRACK);
	pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;
	pRA_T	pRA_Table = &pDM_Odm->DM_RA_Table;
	u2Byte	macid, phydm_macid, client_cnt = 0;
	PSTA_INFO_T	pEntry;
	s4Byte	tmp_val = 0;
	u1Byte	tmp_val_u1 = 0;
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[PHYDM Common MSG] System up time: ((%d sec))----->\n", pDM_Odm->phydm_sys_up_time));

	if (pDM_Odm->bLinked) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("Curr_STA_ID =  0x%x\n", pDM_Odm->curr_station_id));
		
		/*Print RX Rate*/
		if (pDM_Odm->RxRate <= ODM_RATE11M) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[CCK AGC Report] LNA_idx = 0x%x, VGA_idx = 0x%x\n",
				pDM_Odm->cck_lna_idx, pDM_Odm->cck_vga_idx));		
		} else {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[OFDM AGC Report] { 0x%x, 0x%x, 0x%x, 0x%x }\n",
				pDM_Odm->ofdm_agc_idx[0], pDM_Odm->ofdm_agc_idx[1], pDM_Odm->ofdm_agc_idx[2], pDM_Odm->ofdm_agc_idx[3]));	
		}

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("RSSI: { %d,  %d,  %d,  %d },    RxRate:",
			(pDM_Odm->RSSI_A == 0xff) ? 0 : pDM_Odm->RSSI_A , 
			(pDM_Odm->RSSI_B == 0xff) ? 0 : pDM_Odm->RSSI_B , 
			(pDM_Odm->RSSI_C == 0xff) ? 0 : pDM_Odm->RSSI_C, 
			(pDM_Odm->RSSI_D == 0xff) ? 0 : pDM_Odm->RSSI_D));

		phydm_print_rate(pDM_Odm, pDM_Odm->RxRate, ODM_COMP_COMMON);
		
		/*Print TX Rate*/
		for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {
			
			pEntry = pDM_Odm->pODM_StaInfo[macid];
			if (IS_STA_VALID(pEntry)) {
				
				phydm_macid = (pDM_Odm->platform2phydm_macid_table[macid]);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("TXRate [%d]:", macid));
				phydm_print_rate(pDM_Odm, pRA_Table->link_tx_rate[macid], ODM_COMP_COMMON);
				
				client_cnt++;
			
				if (client_cnt == pDM_Odm->number_linked_client)
					break;
			}
		}
		
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("TP { TX, RX, total} = {%d, %d, %d }Mbps, TrafficLoad = (%d))\n", 
			pDM_Odm->tx_tp, pDM_Odm->rx_tp, pDM_Odm->total_tp, pDM_Odm->TrafficLoad));

		tmp_val_u1 = (pCfoTrack->CrystalCap > pCfoTrack->DefXCap) ? (pCfoTrack->CrystalCap - pCfoTrack->DefXCap) : (pCfoTrack->DefXCap - pCfoTrack->CrystalCap);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("CFO_avg = ((%d kHz)) , CrystalCap_tracking = ((%s%d))\n", 
			pCfoTrack->CFO_ave_pre, ((pCfoTrack->CrystalCap > pCfoTrack->DefXCap)?"+":"-"),tmp_val_u1));

		/* Condition number */
#if (RTL8822B_SUPPORT == 1) 
		if (pDM_Odm->SupportICType == ODM_RTL8822B) {
 			tmp_val = phydm_get_condition_number_8822B(pDM_Odm);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("Condition number = ((%d))\n", tmp_val));
		}
#endif

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
		/*STBC or LDPC pkt*/
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("LDPC = %s, STBC = %s\n", (pDM_Odm->PhyDbgInfo.bLdpcPkt)?"Y":"N", (pDM_Odm->PhyDbgInfo.bStbcPkt)?"Y":"N"));
#endif
	} else {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("No Link !!!\n"));
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[CCA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",	
		FalseAlmCnt->Cnt_CCK_CCA, FalseAlmCnt->Cnt_OFDM_CCA, FalseAlmCnt->Cnt_CCA_all));

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[FA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",	
		FalseAlmCnt->Cnt_Cck_fail, FalseAlmCnt->Cnt_Ofdm_fail, FalseAlmCnt->Cnt_all));

	#if (ODM_IC_11N_SERIES_SUPPORT == 1) 
	if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[OFDM FA Detail] Parity_Fail = (( %d )), Rate_Illegal = (( %d )), CRC8_fail = (( %d )), Mcs_fail = (( %d )), Fast_Fsync = (( %d )), SB_Search_fail = (( %d ))\n",	
			FalseAlmCnt->Cnt_Parity_Fail, FalseAlmCnt->Cnt_Rate_Illegal, FalseAlmCnt->Cnt_Crc8_fail, FalseAlmCnt->Cnt_Mcs_fail, FalseAlmCnt->Cnt_Fast_Fsync, FalseAlmCnt->Cnt_SB_Search_fail));
	}
	#endif
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("bLinked = %d, Num_client = %d, RSSI_Min = %d, CurrentIGI = 0x%x, bNoisy=%d\n\n",
		pDM_Odm->bLinked, pDM_Odm->number_linked_client, pDM_Odm->RSSI_Min, pDM_DigTable->CurIGValue, pDM_Odm->NoisyDecision));

/*
	temp_reg = ODM_GetBBReg(pDM_Odm, 0xDD0, bMaskByte0);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("0xDD0 = 0x%x\n",temp_reg));
		
	temp_reg = ODM_GetBBReg(pDM_Odm, 0xDDc, bMaskByte1);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("0xDDD = 0x%x\n",temp_reg));
	
	temp_reg = ODM_GetBBReg(pDM_Odm, 0xc50, bMaskByte0);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("0xC50 = 0x%x\n",temp_reg));

	temp_reg = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x0, 0x3fe0);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("RF 0x0[13:5] = 0x%x\n\n",temp_reg));
*/	

#endif
}


VOID phydm_BasicProfile(
	IN		PVOID			pDM_VOID,
	IN		u4Byte			*_used,
	OUT		char				*output,
	IN		u4Byte			*_out_len
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	char  *Cut = NULL;
	char *ICType = NULL;
	u4Byte used = *_used;
	u4Byte out_len = *_out_len;
	u4Byte	commit_ver = 0;
	u4Byte	date = 0;
	char	*commit_by = NULL;
	u4Byte	release_ver = 0;

	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "% Basic Profile %"));

	if (pDM_Odm->SupportICType == ODM_RTL8188E) {
		#if (RTL8188E_SUPPORT == 1)
		ICType = "RTL8188E";
		date = RELEASE_DATE_8188E;
		commit_by = COMMIT_BY_8188E;
		release_ver = RELEASE_VERSION_8188E;
		#endif
	}
	#if (RTL8812A_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8812) {
		ICType = "RTL8812A";
		date = RELEASE_DATE_8812A;
		commit_by = COMMIT_BY_8812A;
		release_ver = RELEASE_VERSION_8812A;
	}
	#endif
	#if (RTL8821A_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8821) {
		ICType = "RTL8821A";
		date = RELEASE_DATE_8821A;
		commit_by = COMMIT_BY_8821A;
		release_ver = RELEASE_VERSION_8821A;
	}
	#endif
	#if (RTL8192E_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8192E) {
		ICType = "RTL8192E";
		date = RELEASE_DATE_8192E;
		commit_by = COMMIT_BY_8192E;
		release_ver = RELEASE_VERSION_8192E;
	}
	#endif
	#if (RTL8723B_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8723B) {
		ICType = "RTL8723B";
		date = RELEASE_DATE_8723B;
		commit_by = COMMIT_BY_8723B;
		release_ver = RELEASE_VERSION_8723B;
	}
	#endif
	#if (RTL8814A_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8814A) {
		ICType = "RTL8814A";
		date = RELEASE_DATE_8814A;
		commit_by = COMMIT_BY_8814A;
		release_ver = RELEASE_VERSION_8814A;
	}
	#endif
	#if (RTL8881A_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8881A) {
		ICType = "RTL8881A";
		/**/
	}
	#endif
	#if (RTL8822B_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8822B) {
		ICType = "RTL8822B";
		date = RELEASE_DATE_8822B;
		commit_by = COMMIT_BY_8822B;
		release_ver = RELEASE_VERSION_8822B;
	}
	#endif
	#if (RTL8197F_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8197F) {
		ICType = "RTL8197F";
		date = RELEASE_DATE_8197F;
		commit_by = COMMIT_BY_8197F;
		release_ver = RELEASE_VERSION_8197F;
	}
	#endif

	#if (RTL8703B_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8703B) {
		
		ICType = "RTL8703B";
		date = RELEASE_DATE_8703B;
		commit_by = COMMIT_BY_8703B;
		release_ver = RELEASE_VERSION_8703B;
		
	} 
	#endif
	#if (RTL8195A_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8195A) {
		ICType = "RTL8195A";
		/**/		
	}
	#endif
	#if (RTL8188F_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8188F) {
		ICType = "RTL8188F";
		date = RELEASE_DATE_8188F;
		commit_by = COMMIT_BY_8188F;
		release_ver = RELEASE_VERSION_8188F;
	}
	#endif
	#if (RTL8723D_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8723D) {
		ICType = "RTL8723D";
		date = RELEASE_DATE_8723D;
		commit_by = COMMIT_BY_8723D;
		release_ver = RELEASE_VERSION_8723D;
		/**/	
	}
	#endif
	#if (RTL8821C_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8821C) {
		ICType = "RTL8821C";
		date = RELEASE_DATE_8821C;
		commit_by = COMMIT_BY_8821C;
		release_ver = RELEASE_VERSION_8821C;
	}
	#endif
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s (MP Chip: %s)\n", "IC Type", ICType, pDM_Odm->bIsMPChip ? "Yes" : "No"));

	if (pDM_Odm->CutVersion == ODM_CUT_A)			
		Cut = "A";
	else if (pDM_Odm->CutVersion == ODM_CUT_B)            
		Cut = "B";
	else if (pDM_Odm->CutVersion == ODM_CUT_C)            
		Cut = "C";
	else if (pDM_Odm->CutVersion == ODM_CUT_D)            
		Cut = "D";
	else if (pDM_Odm->CutVersion == ODM_CUT_E)            
		Cut = "E";
	else if (pDM_Odm->CutVersion == ODM_CUT_F)            
		Cut = "F";
	else if (pDM_Odm->CutVersion == ODM_CUT_I)            
		Cut = "I";
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Cut Version", Cut));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d\n", "PHY Parameter Version", ODM_GetHWImgVersion(pDM_Odm)));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d\n", "PHY Parameter Commit date", date));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PHY Parameter Commit by", commit_by));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d\n", "PHY Parameter Release Version", release_ver));
	
#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	{
		PADAPTER		       Adapter = pDM_Odm->Adapter;
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d (Subversion: %d)\n", "FW Version", Adapter->MgntInfo.FirmwareVersion, Adapter->MgntInfo.FirmwareSubVersion));
	}
#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
	{
		struct rtl8192cd_priv *priv = pDM_Odm->priv;
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d (Subversion: %d)\n", "FW Version", priv->pshare->fw_version, priv->pshare->fw_sub_version));
	}
#else
	{
		PADAPTER		       Adapter = pDM_Odm->Adapter;
		HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d (Subversion: %d)\n", "FW Version", pHalData->FirmwareVersion, pHalData->FirmwareSubVersion));
	}
#endif
	//1 PHY DM Version List
	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "% PHYDM Version %"));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Code Base", PHYDM_CODE_BASE));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Release Date", PHYDM_RELEASE_DATE));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Adaptivity", ADAPTIVITY_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "DIG", DIG_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Dynamic BB PowerSaving", DYNAMIC_BBPWRSAV_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "CFO Tracking", CFO_TRACKING_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Antenna Diversity", ANTDIV_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Power Tracking", POWRTRACKING_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Dynamic TxPower", DYNAMIC_TXPWR_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "RA Info", RAINFO_VERSION));
#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Antenna Detection", ANTDECT_VERSION));
#endif
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Auto Channel Selection", ACS_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "EDCA Turbo", EDCATURBO_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Path Diversity", PATHDIV_VERSION));

#if (RTL8822B_SUPPORT == 1)  
	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PHY config 8822B", PHY_CONFIG_VERSION_8822B));
	
#endif
#if (RTL8197F_SUPPORT == 1)  
	if (pDM_Odm->SupportICType & ODM_RTL8197F)
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PHY config 8197F", PHY_CONFIG_VERSION_8197F));
#endif
	*_used = used;
	*_out_len = out_len;

}

VOID
phydm_fw_trace_en_h2c(
	IN	PVOID		pDM_VOID,
	IN	BOOLEAN		enable,
	IN	u4Byte		fw_debug_component,	
	IN	u4Byte		monitor_mode,
	IN	u4Byte		macid
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			H2C_Parameter[7] = {0};
	u1Byte			cmd_length;

	if (pDM_Odm->SupportICType & PHYDM_IC_3081_SERIES){
		
		H2C_Parameter[0] = enable;
		H2C_Parameter[1] = (u1Byte)(fw_debug_component & bMaskByte0);
		H2C_Parameter[2] = (u1Byte)((fw_debug_component & bMaskByte1)>>8);
		H2C_Parameter[3] = (u1Byte)((fw_debug_component & bMaskByte2)>>16);
		H2C_Parameter[4] = (u1Byte)((fw_debug_component & bMaskByte3)>>24);
		H2C_Parameter[5] = (u1Byte)monitor_mode;
		H2C_Parameter[6] = (u1Byte)macid;
		cmd_length = 7;

	} else {
	
		H2C_Parameter[0] = enable;
		H2C_Parameter[1] = (u1Byte)monitor_mode;
		H2C_Parameter[2] = (u1Byte)macid;
		cmd_length = 3;
	}

	
	ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("---->\n"));
	if (monitor_mode == 0){
		ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[H2C] FW_debug_en: (( %d ))\n", enable));
	} else {
		ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[H2C] FW_debug_en: (( %d )), mode: (( %d )), macid: (( %d ))\n", enable, monitor_mode, macid));
	}
	ODM_FillH2CCmd(pDM_Odm, PHYDM_H2C_FW_TRACE_EN, cmd_length, H2C_Parameter);
}


#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
BOOLEAN
phydm_api_set_txagc(
	IN	PDM_ODM_T				pDM_Odm,
	IN	u4Byte					PowerIndex,
	IN	ODM_RF_RADIO_PATH_E		Path,	
	IN	u1Byte					HwRate,
	IN	BOOLEAN					bSingleRate
	)
{
	BOOLEAN		ret = FALSE;
	
#if ((RTL8822B_SUPPORT == 1) || (RTL8821C_SUPPORT == 1))
	if (pDM_Odm->SupportICType & (ODM_RTL8822B|ODM_RTL8821C)) {
		if (bSingleRate) {
#if (RTL8822B_SUPPORT == 1)
			if (pDM_Odm->SupportICType == ODM_RTL8822B)
				ret = phydm_write_txagc_1byte_8822b(pDM_Odm, PowerIndex, Path, HwRate);
#endif
#if (RTL8821C_SUPPORT == 1)
			if (pDM_Odm->SupportICType == ODM_RTL8821C)
				ret = phydm_write_txagc_1byte_8821c(pDM_Odm, PowerIndex, Path, HwRate);
#endif
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
			SetCurrentTxAGC(pDM_Odm->priv, Path, HwRate, (u1Byte)PowerIndex);
#endif

		} else {
			u1Byte	i;
#if (RTL8822B_SUPPORT == 1)
			if (pDM_Odm->SupportICType == ODM_RTL8822B)
				ret = config_phydm_write_txagc_8822b(pDM_Odm, PowerIndex, Path, HwRate);
#endif
#if (RTL8821C_SUPPORT == 1)
			if (pDM_Odm->SupportICType == ODM_RTL8821C)
				ret = config_phydm_write_txagc_8821c(pDM_Odm, PowerIndex, Path, HwRate);
#endif
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)		
			for (i = 0; i < 4; i++)
				SetCurrentTxAGC(pDM_Odm->priv, Path, (HwRate + i), (u1Byte)PowerIndex);
#endif
		}
	}
#endif


#if (RTL8197F_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8197F)
		ret = config_phydm_write_txagc_8197f(pDM_Odm, PowerIndex, Path, HwRate);
#endif

	return ret;
}

u1Byte
phydm_api_get_txagc(
	IN	PDM_ODM_T				pDM_Odm,
	IN	ODM_RF_RADIO_PATH_E	Path,
	IN	u1Byte					HwRate
	)
{
	u1Byte	ret = 0;
	
#if (RTL8822B_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		ret = config_phydm_read_txagc_8822b(pDM_Odm, Path, HwRate);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8197F)
		ret = config_phydm_read_txagc_8197f(pDM_Odm, Path, HwRate);
#endif

#if (RTL8821C_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8821C)
		ret = config_phydm_read_txagc_8821c(pDM_Odm, Path, HwRate);
#endif

	return ret;
}


BOOLEAN
phydm_api_switch_bw_channel(
	IN	PDM_ODM_T				pDM_Odm,
	IN	u1Byte					central_ch,
	IN	u1Byte					primary_ch_idx,
	IN	ODM_BW_E				bandwidth
	)
{
	BOOLEAN		ret = FALSE;
	
#if (RTL8822B_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		ret = config_phydm_switch_channel_bw_8822b(pDM_Odm, central_ch, primary_ch_idx, bandwidth);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8197F)
		ret = config_phydm_switch_channel_bw_8197f(pDM_Odm, central_ch, primary_ch_idx, bandwidth);
#endif

#if (RTL8821C_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8821C)
		ret = config_phydm_switch_channel_bw_8821c(pDM_Odm, central_ch, primary_ch_idx, bandwidth);
#endif

	return ret;
}

BOOLEAN
phydm_api_trx_mode(
	IN	PDM_ODM_T				pDM_Odm,
	IN	ODM_RF_PATH_E			TxPath,
	IN	ODM_RF_PATH_E			RxPath,
	IN	BOOLEAN					bTx2Path
	)
{
	BOOLEAN		ret = FALSE;
	
#if (RTL8822B_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		ret = config_phydm_trx_mode_8822b(pDM_Odm, TxPath, RxPath, bTx2Path);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8197F)
		ret = config_phydm_trx_mode_8197f(pDM_Odm, TxPath, RxPath, bTx2Path);
#endif

	return ret;
}
#endif

VOID
phydm_get_per_path_txagc(
	IN		PVOID			pDM_VOID,
	IN		u1Byte			path,
	IN		u4Byte			*_used,
	OUT		char				*output,
	IN		u4Byte			*_out_len
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			rate_idx;
	u1Byte			txagc;
	u4Byte			used = *_used;
	u4Byte			out_len = *_out_len;

#if ((RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1) || (RTL8821C_SUPPORT == 1))
	if (((pDM_Odm->SupportICType & (ODM_RTL8822B|ODM_RTL8197F)) && (path <= ODM_RF_PATH_B)) ||
		((pDM_Odm->SupportICType & (ODM_RTL8821C)) && (path <= ODM_RF_PATH_A))) {
		for (rate_idx = 0; rate_idx <= 0x53; rate_idx++) {
			if (rate_idx == ODM_RATE1M)
				PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s\n", "CCK====>"));
			else if (rate_idx == ODM_RATE6M)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "OFDM====>"));
			else if (rate_idx == ODM_RATEMCS0)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "HT 1ss====>"));
			else if (rate_idx == ODM_RATEMCS8)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "HT 2ss====>"));
			else if (rate_idx == ODM_RATEMCS16)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "HT 3ss====>"));
			else if (rate_idx == ODM_RATEMCS24)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "HT 4ss====>"));
			else if (rate_idx == ODM_RATEVHTSS1MCS0)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "VHT 1ss====>"));
			else if (rate_idx == ODM_RATEVHTSS2MCS0)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "VHT 2ss====>"));
			else if (rate_idx == ODM_RATEVHTSS3MCS0)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "VHT 3ss====>"));
			else if (rate_idx == ODM_RATEVHTSS4MCS0)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "VHT 4ss====>"));
			
			txagc = phydm_api_get_txagc(pDM_Odm, (ODM_RF_RADIO_PATH_E) path, rate_idx);
			if (config_phydm_read_txagc_check(txagc))
				PHYDM_SNPRINTF((output + used, out_len - used, "  0x%02x    ", txagc));
			else
				PHYDM_SNPRINTF((output + used, out_len - used, "  0x%s    ", "xx"));
		}
	}
#endif
}


VOID
phydm_get_txagc(
	IN		PVOID			pDM_VOID,
	IN		u4Byte			*_used,
	OUT		char				*output,
	IN		u4Byte			*_out_len
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			used = *_used;
	u4Byte			out_len = *_out_len;
	
	/* Path-A */
	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "Path-A===================="));
	phydm_get_per_path_txagc(pDM_Odm, ODM_RF_PATH_A, _used, output, _out_len);
	
	/* Path-B */
	PHYDM_SNPRINTF((output + used, out_len - used, "\n%-35s\n", "Path-B===================="));
	phydm_get_per_path_txagc(pDM_Odm, ODM_RF_PATH_B, _used, output, _out_len);

	/* Path-C */
	PHYDM_SNPRINTF((output + used, out_len - used, "\n%-35s\n", "Path-C===================="));
	phydm_get_per_path_txagc(pDM_Odm, ODM_RF_PATH_C, _used, output, _out_len);

	/* Path-D */
	PHYDM_SNPRINTF((output + used, out_len - used, "\n%-35s\n", "Path-D===================="));
	phydm_get_per_path_txagc(pDM_Odm, ODM_RF_PATH_D, _used, output, _out_len);

}

VOID
phydm_set_txagc(
	IN		PVOID			pDM_VOID,
	IN		u4Byte			*const dm_value,
	IN		u4Byte			*_used,
	OUT		char				*output,
	IN		u4Byte			*_out_len
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			used = *_used;
	u4Byte			out_len = *_out_len;

/*dm_value[1] = Path*/
/*dm_value[2] = HwRate*/
/*dm_value[3] = PowerIndex*/

#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
	if (pDM_Odm->SupportICType & (ODM_RTL8822B|ODM_RTL8197F|ODM_RTL8821C)) {
		if (dm_value[1] <= 1) {
			if ((u1Byte)dm_value[2] != 0xff) {
				if (phydm_api_set_txagc(pDM_Odm, dm_value[3], (ODM_RF_RADIO_PATH_E) dm_value[1], (u1Byte)dm_value[2], TRUE))
					PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s%x\n", "Write path-", dm_value[1], "rate index-0x", dm_value[2], " = 0x", dm_value[3]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s\n", "Write path-", (dm_value[1] & 0x1), "rate index-0x", (dm_value[2] & 0x7f), " fail"));
			} else {
				u1Byte	i;
				u4Byte	power_index;
				BOOLEAN	status = TRUE;

				power_index = (dm_value[3] & 0x3f);

				if (pDM_Odm->SupportICType & (ODM_RTL8822B|ODM_RTL8821C)) {
					power_index = (power_index << 24)|(power_index << 16)|(power_index << 8)|(power_index);

					for (i = 0; i < ODM_RATEVHTSS2MCS9; i += 4)
						status = (status & phydm_api_set_txagc(pDM_Odm, power_index, (ODM_RF_RADIO_PATH_E) dm_value[1], i, FALSE));
				} else if (pDM_Odm->SupportICType & ODM_RTL8197F) {
					for (i = 0; i <= ODM_RATEMCS15; i++)
						status = (status & phydm_api_set_txagc(pDM_Odm, power_index, (ODM_RF_RADIO_PATH_E) dm_value[1], i, FALSE));
				}

				if (status)
					PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x\n", "Write all TXAGC of path-", dm_value[1], " = 0x", dm_value[3]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s\n", "Write all TXAGC of path-", dm_value[1], " fail"));	
			}
		} else {
			PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s\n", "Write path-", (dm_value[1] & 0x1), "rate index-0x", (dm_value[2] & 0x7f), " fail"));
		}
	}
#endif
}

VOID
phydm_debug_trace(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		*const dm_value,
	IN		u4Byte		*_used,
	OUT		char		*output,
	IN		u4Byte		*_out_len
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			pre_debug_components, one = 1;
	u4Byte			used = *_used;
	u4Byte			out_len = *_out_len;

	pre_debug_components = pDM_Odm->DebugComponents;

	PHYDM_SNPRINTF((output + used, out_len - used, "\n%s\n", "================================"));
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Debug Message] PhyDM Selection"));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
		PHYDM_SNPRINTF((output + used, out_len - used, "00. (( %s ))DIG\n", ((pDM_Odm->DebugComponents & ODM_COMP_DIG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "01. (( %s ))RA_MASK\n", ((pDM_Odm->DebugComponents & ODM_COMP_RA_MASK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "02. (( %s ))DYNAMIC_TXPWR\n", ((pDM_Odm->DebugComponents & ODM_COMP_DYNAMIC_TXPWR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "03. (( %s ))FA_CNT\n", ((pDM_Odm->DebugComponents & ODM_COMP_FA_CNT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "04. (( %s ))RSSI_MONITOR\n", ((pDM_Odm->DebugComponents & ODM_COMP_RSSI_MONITOR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "05. (( %s ))SNIFFER\n", ((pDM_Odm->DebugComponents & ODM_COMP_SNIFFER) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "06. (( %s ))ANT_DIV\n", ((pDM_Odm->DebugComponents & ODM_COMP_ANT_DIV) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "07. (( %s ))DFS\n", ((pDM_Odm->DebugComponents & ODM_COMP_DFS) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "08. (( %s ))NOISY_DETECT\n", ((pDM_Odm->DebugComponents & ODM_COMP_NOISY_DETECT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "09. (( %s ))RATE_ADAPTIVE\n", ((pDM_Odm->DebugComponents & ODM_COMP_RATE_ADAPTIVE) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "10. (( %s ))PATH_DIV\n", ((pDM_Odm->DebugComponents & ODM_COMP_PATH_DIV) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "12. (( %s ))DYNAMIC_PRICCA\n", ((pDM_Odm->DebugComponents & ODM_COMP_DYNAMIC_PRICCA) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "14. (( %s ))MP\n", ((pDM_Odm->DebugComponents & ODM_COMP_MP) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "15. (( %s ))CFO_TRACKING\n", ((pDM_Odm->DebugComponents & ODM_COMP_CFO_TRACKING) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "16. (( %s ))ACS\n", ((pDM_Odm->DebugComponents & ODM_COMP_ACS) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "17. (( %s ))ADAPTIVITY\n", ((pDM_Odm->DebugComponents & PHYDM_COMP_ADAPTIVITY) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "18. (( %s ))RA_DBG\n", ((pDM_Odm->DebugComponents & PHYDM_COMP_RA_DBG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "19. (( %s ))TXBF\n", ((pDM_Odm->DebugComponents & PHYDM_COMP_TXBF) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "20. (( %s ))EDCA_TURBO\n", ((pDM_Odm->DebugComponents & ODM_COMP_EDCA_TURBO) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "22. (( %s ))FW_DEBUG_TRACE\n", ((pDM_Odm->DebugComponents & ODM_FW_DEBUG_TRACE) ? ("V") : ("."))));
		
		PHYDM_SNPRINTF((output + used, out_len - used, "24. (( %s ))TX_PWR_TRACK\n", ((pDM_Odm->DebugComponents & ODM_COMP_TX_PWR_TRACK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "26. (( %s ))CALIBRATION\n", ((pDM_Odm->DebugComponents & ODM_COMP_CALIBRATION) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "28. (( %s ))PHY_CONFIG\n", ((pDM_Odm->DebugComponents & ODM_PHY_CONFIG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "29. (( %s ))INIT\n", ((pDM_Odm->DebugComponents & ODM_COMP_INIT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "30. (( %s ))COMMON\n", ((pDM_Odm->DebugComponents & ODM_COMP_COMMON) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "31. (( %s ))API\n", ((pDM_Odm->DebugComponents & ODM_COMP_API) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	} else if (dm_value[0] == 101) {
		pDM_Odm->DebugComponents = 0;
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "Disable all debug components"));
	} else {
		if (dm_value[1] == 1) { /*enable*/
			pDM_Odm->DebugComponents |= (one << dm_value[0]);
		} else if (dm_value[1] == 2) { /*disable*/
			pDM_Odm->DebugComponents &= ~(one << dm_value[0]);
		} else
			PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Warning!!!]  1:enable,  2:disable"));
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "pre-DbgComponents = 0x%x\n", pre_debug_components));
	PHYDM_SNPRINTF((output + used, out_len - used, "Curr-DbgComponents = 0x%x\n", pDM_Odm->DebugComponents));
	PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
}

VOID
phydm_fw_debug_trace(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		*const dm_value,
	IN		u4Byte		*_used,
	OUT		char			*output,
	IN		u4Byte		*_out_len
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			pre_fw_debug_components, one = 1;
	u4Byte			used = *_used;
	u4Byte			out_len = *_out_len;

	pre_fw_debug_components = pDM_Odm->fw_DebugComponents;

	PHYDM_SNPRINTF((output + used, out_len - used, "\n%s\n", "================================"));
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[FW Debug Component]"));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
		PHYDM_SNPRINTF((output + used, out_len - used, "00. (( %s ))RA\n", ((pDM_Odm->fw_DebugComponents & PHYDM_FW_COMP_RA) ? ("V") : ("."))));
		
		if (pDM_Odm->SupportICType & PHYDM_IC_3081_SERIES){
			PHYDM_SNPRINTF((output + used, out_len - used, "01. (( %s ))MU\n", ((pDM_Odm->fw_DebugComponents & PHYDM_FW_COMP_MU) ? ("V") : ("."))));
			PHYDM_SNPRINTF((output + used, out_len - used, "02. (( %s ))Path Div\n", ((pDM_Odm->fw_DebugComponents & PHYDM_FW_COMP_PHY_CONFIG) ? ("V") : ("."))));
			PHYDM_SNPRINTF((output + used, out_len - used, "03. (( %s ))Phy Config\n", ((pDM_Odm->fw_DebugComponents & PHYDM_FW_COMP_PHY_CONFIG) ? ("V") : ("."))));
		}
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	} else {
		if (dm_value[0] == 101) {
			pDM_Odm->fw_DebugComponents = 0;
			PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "Clear all fw debug components"));
		} else {
			if (dm_value[1] == 1) { /*enable*/
				pDM_Odm->fw_DebugComponents |= (one << dm_value[0]);
			} else if (dm_value[1] == 2) { /*disable*/
				pDM_Odm->fw_DebugComponents &= ~(one << dm_value[0]);
			} else
				PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Warning!!!]  1:enable,  2:disable"));
		}

		if (pDM_Odm->fw_DebugComponents == 0) {
			pDM_Odm->DebugComponents &= ~ODM_FW_DEBUG_TRACE;
			phydm_fw_trace_en_h2c(pDM_Odm, FALSE, pDM_Odm->fw_DebugComponents, dm_value[2], dm_value[3]); /*H2C to enable C2H Msg*/
		} else {
			pDM_Odm->DebugComponents |= ODM_FW_DEBUG_TRACE;
			phydm_fw_trace_en_h2c(pDM_Odm, TRUE, pDM_Odm->fw_DebugComponents, dm_value[2], dm_value[3]); /*H2C to enable C2H Msg*/
		}	
	}
}

VOID
phydm_DumpBbReg(
	IN		PVOID			pDM_VOID,
	IN		u4Byte			*_used,
	OUT		char				*output,
	IN		u4Byte			*_out_len
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			Addr = 0;
	u4Byte			used = *_used;
	u4Byte			out_len = *_out_len;

	
	/* BB Reg, For Nseries IC we only need to dump page8 to pageF using 3 digits*/
	for (Addr = 0x800; Addr < 0xfff; Addr += 4) {
		if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
			PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%03x 0x%08x,\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord)));
		else
			PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%04x 0x%08x,\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord)));
	}

	if (pDM_Odm->SupportICType & (ODM_RTL8822B | ODM_RTL8814A | ODM_RTL8821C)) {

		if (pDM_Odm->RFType > ODM_2T2R) {
			for (Addr = 0x1800; Addr < 0x18ff; Addr += 4)
				PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%04x 0x%08x,\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord)));
		}

		if (pDM_Odm->RFType > ODM_3T3R) {
			for (Addr = 0x1a00; Addr < 0x1aff; Addr += 4)
				PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%04x 0x%08x,\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord)));
		}

		for (Addr = 0x1900; Addr < 0x19ff; Addr += 4)
			PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%04x 0x%08x,\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord)));

		for (Addr = 0x1c00; Addr < 0x1cff; Addr += 4)
			PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%04x 0x%08x,\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord)));

		for (Addr = 0x1f00; Addr < 0x1fff; Addr += 4)
			PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%04x 0x%08x,\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord)));
	}
}

VOID
phydm_DumpAllReg(
	IN		PVOID			pDM_VOID,
	IN		u4Byte			*_used,
	OUT		char				*output,
	IN		u4Byte			*_out_len
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			Addr = 0;
	u4Byte			used = *_used;
	u4Byte			out_len = *_out_len;

	/* dump MAC register */
	PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "MAC==========\n"));
	for (Addr = 0; Addr < 0x7ff; Addr += 4)
		PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%04x 0x%08x,\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord)));

	for (Addr = 0x1000; Addr < 0x17ff; Addr += 4)
		PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%04x 0x%08x,\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord)));

	/* dump BB register */
	PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "BB==========\n"));
	phydm_DumpBbReg(pDM_Odm, &used, output, &out_len);

	/* dump RF register */
	PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "RF-A==========\n"));
	for (Addr = 0; Addr < 0xFF; Addr++)
		PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%02x 0x%05x,\n", Addr, ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, Addr, bRFRegOffsetMask)));

	if (pDM_Odm->RFType > ODM_1T1R) {
		PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "RF-B==========\n"));
		for (Addr = 0; Addr < 0xFF; Addr++)
			PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%02x 0x%05x,\n", Addr, ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, Addr, bRFRegOffsetMask)));
	}

	if (pDM_Odm->RFType > ODM_2T2R) {
		PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "RF-C==========\n"));
		for (Addr = 0; Addr < 0xFF; Addr++)
			PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%02x 0x%05x,\n", Addr, ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_C, Addr, bRFRegOffsetMask)));
	}

	if (pDM_Odm->RFType > ODM_3T3R) {
		PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "RF-D==========\n"));
		for (Addr = 0; Addr < 0xFF; Addr++)
			PHYDM_VAST_INFO_SNPRINTF((output+used, out_len-used, "0x%02x 0x%05x,\n", Addr, ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_D, Addr, bRFRegOffsetMask)));
	}
}

VOID
phydm_EnableBigJump(
	IN	PDM_ODM_T	pDM_Odm,
	IN	BOOLEAN		state
	)
{
#if (RTL8822B_SUPPORT == 1)
	pDIG_T			pDM_DigTable = &pDM_Odm->DM_DigTable;

	if (state == FALSE) {
		pDM_Odm->DM_DigTable.enableAdjustBigJump = FALSE;
		ODM_SetBBReg(pDM_Odm, 0x8c8, 0xfe, ((pDM_DigTable->bigJumpStep3<<5)|(pDM_DigTable->bigJumpStep2<<3)|pDM_DigTable->bigJumpStep1));
	} else
		pDM_Odm->DM_DigTable.enableAdjustBigJump = TRUE;
#endif
}

#if (RTL8822B_SUPPORT == 1)  

VOID
phydm_showRxRate(
	IN	PDM_ODM_T			pDM_Odm,
	IN		u4Byte			*_used,
	OUT		char				*output,
	IN		u4Byte			*_out_len
	)
{
	u4Byte			used = *_used;
	u4Byte			out_len = *_out_len;

	PHYDM_SNPRINTF((output+used, out_len-used, "=====Rx SU Rate Statistics=====\n"));
	PHYDM_SNPRINTF((output+used, out_len-used, "1SS MCS0 = %d, 1SS MCS1 = %d, 1SS MCS2 = %d, 1SS MCS 3 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryVhtPkt[0], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[1], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[2], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[3]));
	PHYDM_SNPRINTF((output+used, out_len-used, "1SS MCS4 = %d, 1SS MCS5 = %d, 1SS MCS6 = %d, 1SS MCS 7 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryVhtPkt[4], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[5], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[6], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[7]));
	PHYDM_SNPRINTF((output+used, out_len-used, "1SS MCS8 = %d, 1SS MCS9 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryVhtPkt[8], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[9]));
	PHYDM_SNPRINTF((output+used, out_len-used, "2SS MCS0 = %d, 2SS MCS1 = %d, 2SS MCS2 = %d, 2SS MCS 3 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryVhtPkt[10], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[11], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[12], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[13]));
	PHYDM_SNPRINTF((output+used, out_len-used, "2SS MCS4 = %d, 2SS MCS5 = %d, 2SS MCS6 = %d, 2SS MCS 7 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryVhtPkt[14], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[15], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[16], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[17]));
	PHYDM_SNPRINTF((output+used, out_len-used, "2SS MCS8 = %d, 2SS MCS9 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryVhtPkt[18], pDM_Odm->PhyDbgInfo.NumQryVhtPkt[19]));
			
	PHYDM_SNPRINTF((output+used, out_len-used, "=====Rx MU Rate Statistics=====\n"));
	PHYDM_SNPRINTF((output+used, out_len-used, "1SS MCS0 = %d, 1SS MCS1 = %d, 1SS MCS2 = %d, 1SS MCS 3 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[0], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[1], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[2], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[3]));
	PHYDM_SNPRINTF((output+used, out_len-used, "1SS MCS4 = %d, 1SS MCS5 = %d, 1SS MCS6 = %d, 1SS MCS 7 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[4], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[5], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[6], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[7]));
	PHYDM_SNPRINTF((output+used, out_len-used, "1SS MCS8 = %d, 1SS MCS9 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[8], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[9]));
	PHYDM_SNPRINTF((output+used, out_len-used, "2SS MCS0 = %d, 2SS MCS1 = %d, 2SS MCS2 = %d, 2SS MCS 3 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[10], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[11], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[12], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[13]));
	PHYDM_SNPRINTF((output+used, out_len-used, "2SS MCS4 = %d, 2SS MCS5 = %d, 2SS MCS6 = %d, 2SS MCS 7 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[14], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[15], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[16], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[17]));
	PHYDM_SNPRINTF((output+used, out_len-used, "2SS MCS8 = %d, 2SS MCS9 = %d\n",
		pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[18], pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[19]));

}

#endif


struct _PHYDM_COMMAND {
	char name[16];
	u1Byte id;
};

enum PHYDM_CMD_ID {
	PHYDM_HELP,
	PHYDM_DEMO,
	PHYDM_RA,
	PHYDM_PROFILE,
	PHYDM_ANTDIV,
	PHYDM_PATHDIV,
	PHYDM_DEBUG,
	PHYDM_FW_DEBUG,
	PHYDM_SUPPORT_ABILITY,
	PHYDM_GET_TXAGC,
	PHYDM_SET_TXAGC,
	PHYDM_SMART_ANT,
	PHYDM_API,
	PHYDM_TRX_PATH,
	PHYDM_LA_MODE,
	PHYDM_DUMP_REG,
	PHYDM_MU_MIMO,
	PHYDM_HANG,
	PHYDM_BIG_JUMP,
	PHYDM_SHOW_RXRATE,
	PHYDM_NBI_EN,
	PHYDM_CSI_MASK_EN,
	PHYDM_DFS,
	PHYDM_IQK,
	PHYDM_NHM,
	PHYDM_CLM,
	PHYDM_BB_INFO,
	PHYDM_TXBF,
	PHYDM_PAUSE_DIG_EN,
	PHYDM_H2C,
	PHYDM_ANT_SWITCH
};

struct _PHYDM_COMMAND phy_dm_ary[] = {
	{"-h", PHYDM_HELP},		/*do not move this element to other position*/
	{"demo", PHYDM_DEMO},	/*do not move this element to other position*/
	{"ra", PHYDM_RA},
	{"profile", PHYDM_PROFILE},
	{"antdiv", PHYDM_ANTDIV},
	{"pathdiv", PHYDM_PATHDIV},
	{"dbg", PHYDM_DEBUG},
	{"fw_dbg", PHYDM_FW_DEBUG},
	{"ability", PHYDM_SUPPORT_ABILITY},
	{"get_txagc", PHYDM_GET_TXAGC},
	{"set_txagc", PHYDM_SET_TXAGC},
	{"smtant", PHYDM_SMART_ANT},
	{"api", PHYDM_API},
	{"trxpath", PHYDM_TRX_PATH},
	{"lamode", PHYDM_LA_MODE},
	{"dumpreg", PHYDM_DUMP_REG},
	{"mu", PHYDM_MU_MIMO},
	{"hang", PHYDM_HANG},
	{"bigjump", PHYDM_BIG_JUMP},
	{"rxrate", PHYDM_SHOW_RXRATE},
	{"nbi", PHYDM_NBI_EN},
	{"csi_mask", PHYDM_CSI_MASK_EN},
	{"dfs", PHYDM_DFS},
	{"iqk", PHYDM_IQK},
	{"nhm", PHYDM_NHM},
	{"clm", PHYDM_CLM},
	{"bbinfo", PHYDM_BB_INFO},
	{"txbf", PHYDM_TXBF},
	{"pause_dig", PHYDM_PAUSE_DIG_EN},
	{"h2c", PHYDM_H2C},
	{"ant_switch", PHYDM_ANT_SWITCH}
};

VOID
phydm_cmd_parser(
	IN PDM_ODM_T	pDM_Odm,
	IN char		input[][MAX_ARGV],
	IN u4Byte	input_num,
	IN u1Byte	flag,
	OUT char	*output,
	IN u4Byte	out_len
)
{
	u4Byte used = 0;
	u1Byte id = 0;
	int var1[10] = {0};
	int i, input_idx = 0, phydm_ary_size;
	char help[] = "-h";

	if (flag == 0) {
		PHYDM_SNPRINTF((output + used, out_len - used, "GET, nothing to print\n"));
		return;
	}

	PHYDM_SNPRINTF((output + used, out_len - used, "\n"));

	//Parsing Cmd ID
	if (input_num) {

		phydm_ary_size = sizeof(phy_dm_ary) / sizeof(struct _PHYDM_COMMAND);
		for (i = 0; i < phydm_ary_size; i++) {
			if (strcmp(phy_dm_ary[i].name, input[0]) == 0) {
				id = phy_dm_ary[i].id;
				break;
			}
		}
		if (i == phydm_ary_size) {
			PHYDM_SNPRINTF((output + used, out_len - used, "SET, command not found!\n"));
			return;
		}
	}

	switch (id) {

	case PHYDM_HELP:
	{
		PHYDM_SNPRINTF((output + used, out_len - used, "BB cmd ==>\n"));
		for (i=0; i < phydm_ary_size-2; i++) {

				PHYDM_SNPRINTF((output + used, out_len - used, "  %-5d: %s\n", i, phy_dm_ary[i+2].name));
				/**/
		}
	}
	break;

	case PHYDM_DEMO: /*echo demo 10 0x3a z abcde >cmd*/
	{
		u4Byte directory = 0;

		#if (DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_AP))				
		char char_temp;
		#else
		u4Byte char_temp = ' ';
		#endif

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &directory);
		PHYDM_SNPRINTF((output + used, out_len - used, "Decimal Value = %d\n", directory));
		PHYDM_SSCANF(input[2], DCMD_HEX, &directory);
		PHYDM_SNPRINTF((output + used, out_len - used, "Hex Value = 0x%x\n", directory));
		PHYDM_SSCANF(input[3], DCMD_CHAR, &char_temp);
		PHYDM_SNPRINTF((output + used, out_len - used, "Char = %c\n", char_temp));
		PHYDM_SNPRINTF((output + used, out_len - used, "String = %s\n", input[4]));
	}
	break;

	case PHYDM_RA:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);

				/*PHYDM_SNPRINTF((output + used, out_len - used, "new SET, RA_var[%d]= (( %d ))\n", i , var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "odm_RA_debug\n"));*/
			#if (defined(CONFIG_RA_DBG_CMD))
			odm_RA_debug((PVOID)pDM_Odm, (pu4Byte) var1);
			#else
			phydm_RA_debug_PCR(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
			#endif
		}


		break;
		
	case PHYDM_ANTDIV:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);

				/*PHYDM_SNPRINTF((output+used, out_len-used, "new SET, PATHDIV_var[%d]= (( %d ))\n", i , var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "odm_PATHDIV_debug\n"));*/
			#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
			phydm_antdiv_debug(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
			#endif
		}

		break;

	case PHYDM_PATHDIV:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);

				/*PHYDM_SNPRINTF((output+used, out_len-used, "new SET, PATHDIV_var[%d]= (( %d ))\n", i , var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "odm_PATHDIV_debug\n"));*/
#if (defined(CONFIG_PATH_DIVERSITY))
			odm_pathdiv_debug(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
#endif
		}

		break;

	case PHYDM_DEBUG:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);

				/*PHYDM_SNPRINTF((output+used, out_len-used, "new SET, Debug_var[%d]= (( %d ))\n", i , var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "odm_debug_comp\n"));*/
			phydm_debug_trace(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
		}


		break;
		
	case PHYDM_FW_DEBUG:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) 
			phydm_fw_debug_trace(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);

		break;
		
	case PHYDM_SUPPORT_ABILITY:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);

				/*PHYDM_SNPRINTF((output+used, out_len-used, "new SET, support ablity_var[%d]= (( %d ))\n", i , var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "support ablity\n"));*/
			phydm_support_ability_debug(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
		}

		break;
		
	case PHYDM_SMART_ANT:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
			#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
			phydm_hl_smart_ant_debug(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
			#endif
			#endif
		}

		break;

	case PHYDM_API:
#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
	{
		if (pDM_Odm->SupportICType & (ODM_RTL8822B|ODM_RTL8197F | ODM_RTL8821C)) {
			BOOLEAN	bEnableDbgMode;
			u1Byte central_ch, primary_ch_idx, bandwidth;
			
			for (i = 0; i < 4; i++) {
				if (input[i + 1])
					PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
			}
			
			bEnableDbgMode = (BOOLEAN)var1[0];
			central_ch = (u1Byte) var1[1];
			primary_ch_idx = (u1Byte) var1[2];
			bandwidth = (ODM_BW_E) var1[3];

			if (bEnableDbgMode) {
				pDM_Odm->bDisablePhyApi = FALSE;
				phydm_api_switch_bw_channel(pDM_Odm, central_ch, primary_ch_idx, (ODM_BW_E) bandwidth);
				pDM_Odm->bDisablePhyApi = TRUE;
				PHYDM_SNPRINTF((output+used, out_len-used, "central_ch = %d, primary_ch_idx = %d, bandwidth = %d\n", central_ch, primary_ch_idx, bandwidth));
			} else {
				pDM_Odm->bDisablePhyApi = FALSE;
				PHYDM_SNPRINTF((output+used, out_len-used, "Disable API debug mode\n"));
			}
		} else
			PHYDM_SNPRINTF((output+used, out_len-used, "This IC doesn't support PHYDM API function\n"));
	}
#else
		PHYDM_SNPRINTF((output+used, out_len-used, "This IC doesn't support PHYDM API function\n"));
#endif
		break;	
		
	case PHYDM_PROFILE: /*echo profile, >cmd*/
		phydm_BasicProfile(pDM_Odm, &used, output, &out_len);
		break;

	case PHYDM_GET_TXAGC:
		phydm_get_txagc(pDM_Odm, &used, output, &out_len);
		break;
		
	case PHYDM_SET_TXAGC:
	{
		BOOLEAN		bEnableDbgMode;
		
		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}
		
		if ((strcmp(input[1], help) == 0)) {
			PHYDM_SNPRINTF((output+used, out_len-used, "{En} {pathA~D(0~3)} {rate_idx(Hex), All_rate:0xff} {txagc_idx (Hex)}\n"));
			/**/
			
		} else {

			bEnableDbgMode = (BOOLEAN)var1[0];
			if (bEnableDbgMode) {
				pDM_Odm->bDisablePhyApi = FALSE;
				phydm_set_txagc(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
				pDM_Odm->bDisablePhyApi = TRUE;
			} else {
				pDM_Odm->bDisablePhyApi = FALSE;
				PHYDM_SNPRINTF((output+used, out_len-used, "Disable API debug mode\n"));
			}
		}
	}
		break;
		
	case PHYDM_TRX_PATH:

		for (i = 0; i < 4; i++) {
			if (input[i + 1])
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
		}
		#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1)
		if (pDM_Odm->SupportICType & (ODM_RTL8822B|ODM_RTL8197F)) {
			u1Byte		TxPath, RxPath;
			BOOLEAN		bEnableDbgMode, bTx2Path;
			
			bEnableDbgMode = (BOOLEAN)var1[0];
			TxPath = (u1Byte) var1[1];
			RxPath = (u1Byte) var1[2];
			bTx2Path = (BOOLEAN) var1[3];

			if (bEnableDbgMode) {
				pDM_Odm->bDisablePhyApi = FALSE;
				phydm_api_trx_mode(pDM_Odm, (ODM_RF_PATH_E) TxPath, (ODM_RF_PATH_E) RxPath, bTx2Path);
				pDM_Odm->bDisablePhyApi = TRUE;
				PHYDM_SNPRINTF((output+used, out_len-used, "TxPath = 0x%x, RxPath = 0x%x, bTx2Path = %d\n", TxPath, RxPath, bTx2Path));
			} else {
				pDM_Odm->bDisablePhyApi = FALSE;
				PHYDM_SNPRINTF((output+used, out_len-used, "Disable API debug mode\n"));
			}
		} else
		#endif
			phydm_config_trx_path(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);

		break;

	case PHYDM_LA_MODE:
#if (PHYDM_LA_MODE_SUPPORT == 1)
	
		if (pDM_Odm->SupportICType & PHYDM_IC_SUPPORT_LA_MODE) {
			u2Byte		PollingTime;
			u1Byte		TrigSel, TrigSigSel, DmaDataSigSel, TriggerTime_unit_num;
			BOOLEAN		bEnableLaMode, bTriggerEdge;
			u4Byte		DbgPort, TriggerTime_mu_sec;
			u1Byte		sampling_rate = 0;

			for (i = 0; i < 6; i++) {
				if (input[i + 1])
					PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
			}

			bEnableLaMode = (BOOLEAN)var1[0];
			DbgPrint("echo cmd input_num = %d\n", input_num);
			/* if ((strcmp(input[1], help) == 0) && (input_num == 2)) {*/
			if ((strcmp(input[1], help) == 0)) {
				PHYDM_SNPRINTF((output+used, out_len-used, "{En} {0:BB,1:MAC} {BB:DbgPort[bit],MAC:0-ok/1-fail/2-cca} {RptFormat} {TrigTime} \n {PollingTime} {DbgPort} {0:P_Edge, 1:N_Edge} {SpRate:0-80M,1-40M,2-20M} {Capture num}\n"));
				/**/
			/*} else if ((bEnableLaMode == 1) && (input_num == 10)) {*/
			} else if ((bEnableLaMode == 1)) {
				TrigSel = (u1Byte)var1[1]; /*0: BB, 1: MAC*/
				TrigSigSel = (u1Byte)var1[2];
				DmaDataSigSel = (u1Byte)var1[3];
				TriggerTime_mu_sec = var1[4]; /*unit: us*/
				PollingTime = (((u1Byte)var1[5]) << 6); /*unit: ms*/
				
				PHYDM_SSCANF(input[7], DCMD_HEX, &var1[6]);
				PHYDM_SSCANF(input[8], DCMD_DECIMAL, &var1[7]);
				PHYDM_SSCANF(input[9], DCMD_DECIMAL, &var1[8]);
				PHYDM_SSCANF(input[10], DCMD_DECIMAL, &var1[9]);

				DbgPort = (u4Byte)var1[6];
				bTriggerEdge = (BOOLEAN) var1[7];
				sampling_rate = var1[8] & 0x7;

				#if 1
				TriggerTime_unit_num = phydm_la_mode_mac_setting(pDM_Odm, TriggerTime_mu_sec);
				phydm_la_mode_bb_setting(pDM_Odm, DbgPort, bTriggerEdge, sampling_rate);
				#else
				if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {
					ODM_SetBBReg(pDM_Odm, 0x198C , BIT2|BIT1|BIT0, 7); /*disable dbg clk gating*/
					ODM_SetBBReg(pDM_Odm, 0x8FC, bMaskDWord, DbgPort);
					ODM_SetBBReg(pDM_Odm, 0x95C , BIT31, bTriggerEdge); /*0: posedge, 1: negedge*/
					ODM_SetBBReg(pDM_Odm, 0x95c, 0xe0, sampling_rate);
				} else {
					ODM_SetBBReg(pDM_Odm, 0x908, bMaskDWord, DbgPort);
					ODM_SetBBReg(pDM_Odm, 0x9A0 , BIT31, bTriggerEdge); /*0: posedge, 1: negedge*/
					ODM_SetBBReg(pDM_Odm, 0x9A0, 0xe0, sampling_rate);
				}
				#endif

				pDM_Odm->ADCSmp_dbg_port = DbgPort;
				pDM_Odm->ADCSmp_trigger_edge = bTriggerEdge;
				pDM_Odm->ADCsmp_smp_rate = sampling_rate;
				pDM_Odm->ADCSmp_count = var1[9];

				DbgPrint("echo lamode %d %d %d %d %d %d %x %d %d %d\n", var1[0], var1[1], var1[2], var1[3], var1[4], var1[5], var1[6], var1[7], var1[8], var1[9]);
				
				ADCSmp_Set(pDM_Odm, (RT_ADCSMP_TRIG_SEL) TrigSel, (RT_ADCSMP_TRIG_SIG_SEL) TrigSigSel, DmaDataSigSel, TriggerTime_unit_num, PollingTime);

				PHYDM_SNPRINTF((output+used, out_len-used, "TrigSel = ((%s)), TrigSigSel = ((%d)), DmaDataSigSel = ((%d))\n", ((TrigSel)?"MAC":"BB"), TrigSigSel, DmaDataSigSel));
				PHYDM_SNPRINTF((output+used, out_len-used, "TriggerTime = ((%d * %d us)), PollingTime = ((%d)), sampling rate = ((%d MHz))\n", 
					TriggerTime_unit_num, ((pDM_Odm->ADCsmp_time_unit == 0) ? 1 : (2<<(pDM_Odm->ADCsmp_time_unit-1))), PollingTime, (80>>sampling_rate)));
			} else {
				ADCSmp_Stop(pDM_Odm);
				PHYDM_SNPRINTF((output+used, out_len-used, "Disable LA mode\n"));
			}
		} else
		#endif
			PHYDM_SNPRINTF((output+used, out_len-used, "This IC doesn't support LA mode\n"));
		break;

	case PHYDM_DUMP_REG:
	{
		u1Byte	type = 0;
		
		if (input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			type = (u1Byte)var1[0];
		}

		if (type == 0)
			phydm_DumpBbReg(pDM_Odm, &used, output, &out_len);
		else if (type == 1)
			phydm_DumpAllReg(pDM_Odm, &used, output, &out_len);
	}
		break;

	case PHYDM_MU_MIMO:
#if (RTL8822B_SUPPORT == 1)
		
		if (input[1])
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
		else
			var1[0] = 0;
		
		if (var1[0] == 1) {
			int index, ptr;
			u4Byte Dword_H, Dword_L;

			PHYDM_SNPRINTF((output+used, out_len-used, "Get MU BFee CSI\n"));
			ODM_SetBBReg(pDM_Odm, 0x9e8, BIT17|BIT16, 2); /*Read BFee*/
			ODM_SetBBReg(pDM_Odm, 0x1910, BIT15, 1); /*Select BFee's CSI report*/
			ODM_SetBBReg(pDM_Odm, 0x19b8, BIT6, 1); /*set as CSI report*/
			ODM_SetBBReg(pDM_Odm, 0x19a8, 0xFFFF, 0xFFFF); /*disable gated_clk*/

			for (index = 0; index < 80; index++) {
				ptr = index + 256;
				if (ptr > 311)
					ptr -= 312;
				ODM_SetBBReg(pDM_Odm, 0x1910, 0x03FF0000, ptr); /*Select Address*/
				Dword_H = ODM_GetBBReg(pDM_Odm, 0xF74, bMaskDWord);
				Dword_L = ODM_GetBBReg(pDM_Odm, 0xF5C, bMaskDWord);
				if (index % 2 == 0)
					PHYDM_SNPRINTF((output+used, out_len-used, "%02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n", 
					Dword_L & bMaskByte0, (Dword_L & bMaskByte1) >> 8, (Dword_L & bMaskByte2) >> 16, (Dword_L & bMaskByte3) >> 24,
					Dword_H & bMaskByte0, (Dword_H & bMaskByte1) >> 8, (Dword_H & bMaskByte2) >> 16, (Dword_H & bMaskByte3) >> 24));
				else
					PHYDM_SNPRINTF((output+used, out_len-used, "%02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n", 
					Dword_L & bMaskByte0, (Dword_L & bMaskByte1) >> 8, (Dword_L & bMaskByte2) >> 16, (Dword_L & bMaskByte3) >> 24,
					Dword_H & bMaskByte0, (Dword_H & bMaskByte1) >> 8, (Dword_H & bMaskByte2) >> 16, (Dword_H & bMaskByte3) >> 24));
			}
		} else if (var1[0] == 2) {
			int index, ptr;
			u4Byte Dword_H, Dword_L;

			PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var1[1]);
			PHYDM_SNPRINTF((output+used, out_len-used, "Get MU BFer's STA%d CSI\n", var1[1]));
			ODM_SetBBReg(pDM_Odm, 0x9e8, BIT24, 0); /*Read BFer*/
			ODM_SetBBReg(pDM_Odm, 0x9e8, BIT25, 1); /*enable Read/Write RAM*/
			ODM_SetBBReg(pDM_Odm, 0x9e8, BIT30|BIT29|BIT28, var1[1]); /*read which STA's CSI report*/
			ODM_SetBBReg(pDM_Odm, 0x1910, BIT15, 0); /*select BFer's CSI*/
			ODM_SetBBReg(pDM_Odm, 0x19e0, 0x00003FC0, 0xFF); /*disable gated_clk*/

			for (index = 0; index < 80; index++) {
				ptr = index + 256;
				if (ptr > 311)
					ptr -= 312;
				ODM_SetBBReg(pDM_Odm, 0x1910, 0x03FF0000, ptr); /*Select Address*/
				Dword_H = ODM_GetBBReg(pDM_Odm, 0xF74, bMaskDWord);
				Dword_L = ODM_GetBBReg(pDM_Odm, 0xF5C, bMaskDWord);
				if (index % 2 == 0)
					PHYDM_SNPRINTF((output+used, out_len-used, "%02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n", 
					Dword_L & bMaskByte0, (Dword_L & bMaskByte1) >> 8, (Dword_L & bMaskByte2) >> 16, (Dword_L & bMaskByte3) >> 24,
					Dword_H & bMaskByte0, (Dword_H & bMaskByte1) >> 8, (Dword_H & bMaskByte2) >> 16, (Dword_H & bMaskByte3) >> 24));
				else
					PHYDM_SNPRINTF((output+used, out_len-used, "%02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n", 
					Dword_L & bMaskByte0, (Dword_L & bMaskByte1) >> 8, (Dword_L & bMaskByte2) >> 16, (Dword_L & bMaskByte3) >> 24,
					Dword_H & bMaskByte0, (Dword_H & bMaskByte1) >> 8, (Dword_H & bMaskByte2) >> 16, (Dword_H & bMaskByte3) >> 24));
				
				PHYDM_SNPRINTF((output+used, out_len-used, "ptr=%d : 0x%8x  %8x\n", ptr, Dword_H, Dword_L));
			}

		}
#endif
		break;

	case PHYDM_BIG_JUMP:
	{
#if (RTL8822B_SUPPORT == 1)
		if (input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			phydm_EnableBigJump(pDM_Odm, (BOOLEAN)(var1[0]));
		} else
			PHYDM_SNPRINTF((output + used, out_len - used, "unknown command!\n"));
#else
		PHYDM_SNPRINTF((output + used, out_len - used, "The command is only for 8822B!\n"));
#endif
		break;
	}
	
	case PHYDM_HANG:
		phydm_BB_RxHang_Info(pDM_Odm, &used, output, &out_len);
		break;

	case PHYDM_SHOW_RXRATE:
#if (RTL8822B_SUPPORT == 1)
	{
		u1Byte	rate_idx;
	
		if (input[1])
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (var1[0] == 1)
			phydm_showRxRate(pDM_Odm, &used, output, &out_len);
		else {
			PHYDM_SNPRINTF((output+used, out_len-used, "Reset Rx rate counter\n"));

			for (rate_idx = 0; rate_idx < 40; rate_idx++) {
				pDM_Odm->PhyDbgInfo.NumQryVhtPkt[rate_idx] = 0;
				pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[rate_idx] = 0;
			}
		}
	}	
#endif
	break;

	case PHYDM_NBI_EN:
		
		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			
			phydm_api_debug(pDM_Odm, PHYDM_API_NBI, (u4Byte *)var1, &used, output, &out_len);
			/**/
		}


		break;

	case PHYDM_CSI_MASK_EN:
		
		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			
			phydm_api_debug(pDM_Odm, PHYDM_API_CSI_MASK, (u4Byte *)var1, &used, output, &out_len);
			/**/
		}


		break;	

	case PHYDM_DFS:
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	{
		u4Byte var[6] = {0};

		for (i = 0; i < 6; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var[i]);
				input_idx++;
			}
		}
	
		if (input_idx >= 1)
			phydm_dfs_debug(pDM_Odm, var, &used, output, &out_len);
	}
#endif
		break;

	case PHYDM_IQK:
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
		PHY_IQCalibrate(pDM_Odm->priv);
		PHYDM_SNPRINTF((output + used, out_len - used, "IQK !!\n"));
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		PHY_IQCalibrate(pDM_Odm->Adapter, FALSE);
		PHYDM_SNPRINTF((output + used, out_len - used, "IQK !!\n"));
#endif
		break;

	case PHYDM_NHM:
	{	
		u1Byte		target_rssi;
		u4Byte		value32;
		u2Byte		nhm_period = 0xC350;	//200ms
		u1Byte		IGI;
		PCCX_INFO	CCX_INFO = &pDM_Odm->DM_CCX_INFO;

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if(input_num == 1) {

			CCX_INFO->echo_NHM_en = FALSE;			
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Trigger NHM: echo nhm 1\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r (Exclude CCA)\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Trigger NHM: echo nhm 2\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r (Include CCA)\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Get NHM results: echo nhm 3\n"));
			
			return;
		}

		/* NMH trigger */
		if ((var1[0] <= 2) && (var1[0] != 0)) {

			CCX_INFO->echo_NHM_en = TRUE;
			CCX_INFO->echo_IGI = (u1Byte)ODM_GetBBReg(pDM_Odm, 0xC50, bMaskByte0);

			target_rssi = CCX_INFO->echo_IGI - 10;

			CCX_INFO->NHM_th[0] = (target_rssi -15 + 10) * 2;

			for(i = 1; i <= 10; i ++) {
				CCX_INFO->NHM_th[i] = CCX_INFO->NHM_th[0] + 6 * i;
			}

			//4 1. store previous NHM setting
			phydm_NHMsetting(pDM_Odm, STORE_NHM_SETTING);

			//4 2. Set NHM period, 0x990[31:16]=0xC350, Time duration for NHM unit: 4us, 0xC350=200ms
			CCX_INFO->NHM_period = nhm_period;

			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Monitor NHM for %d us", nhm_period*4));

			//4 3. Set NHM inexclude_txon, inexclude_cca, ccx_en

			
			CCX_INFO->NHM_inexclude_cca = (var1[0] == 1) ? NHM_EXCLUDE_CCA : NHM_INCLUDE_CCA;
			CCX_INFO->NHM_inexclude_txon = NHM_EXCLUDE_TXON;
			
			phydm_NHMsetting(pDM_Odm, SET_NHM_SETTING);

			for(i = 0; i <= 10; i ++) {
				
				if (i == 5) {
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n NHM_th[%d] = 0x%x, echo_IGI = 0x%x", i, CCX_INFO->NHM_th[i], CCX_INFO->echo_IGI));
				}
				else if (i == 10)
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n NHM_th[%d] = 0x%x\n", i, CCX_INFO->NHM_th[i]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n NHM_th[%d] = 0x%x", i, CCX_INFO->NHM_th[i]));
			}

			//4 4. Trigger NHM
			phydm_NHMtrigger(pDM_Odm);

		}
		
		/*Get NHM results*/
		else if (var1[0] == 3) {

			IGI = (u1Byte)ODM_GetBBReg(pDM_Odm, 0xC50, bMaskByte0);

			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Cur_IGI = 0x%x", IGI));			

			phydm_getNHMresult(pDM_Odm);
			
			//4 Resotre NHM setting
			phydm_NHMsetting(pDM_Odm, RESTORE_NHM_SETTING);
			
			for(i = 0; i <= 11; i++) {
				
				if (i == 5) 			
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n nhm_result[%d] = %d, echo_IGI = 0x%x", i, CCX_INFO->NHM_result[i], CCX_INFO->echo_IGI));
				else if (i == 11)
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n nhm_result[%d] = %d\n", i, CCX_INFO->NHM_result[i]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n nhm_result[%d] = %d", i, CCX_INFO->NHM_result[i]));
			}
			
			CCX_INFO->echo_NHM_en = FALSE;
		}
		else {
					
			CCX_INFO->echo_NHM_en = FALSE;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Trigger NHM: echo nhm 1\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r (Exclude CCA)\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Trigger NHM: echo nhm 2\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r (Include CCA)\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Get NHM results: echo nhm 3\n"));
			
			return;
		}
	}
	break;

	case PHYDM_CLM:
	{
		PCCX_INFO	CCX_INFO = &pDM_Odm->DM_CCX_INFO;
		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		//PHYDM_SNPRINTF((output + used, out_len - used, "\r\n input_num = %d\n", input_num));

		if (input_num == 1) {

			CCX_INFO->echo_CLM_en = FALSE;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Trigger CLM: echo clm 1\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Get CLM results: echo clm 2\n"));
			return;
		}

		/* Set & trigger CLM */
		if (var1[0] == 1) {

			CCX_INFO->echo_CLM_en = TRUE;
			CCX_INFO->CLM_period = 0xC350;		/*100ms*/
			phydm_CLMsetting(pDM_Odm);
			phydm_CLMtrigger(pDM_Odm);				
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Monitor CLM for 200ms\n"));
		}
		
		/* Get CLM results */
		else if (var1[0] == 2) {

			CCX_INFO->echo_CLM_en = FALSE;
			phydm_getCLMresult(pDM_Odm);
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n CLM_result = %d us\n", CCX_INFO->CLM_result*4));
			
		} else {

			CCX_INFO->echo_CLM_en = FALSE;
			PHYDM_SNPRINTF((output + used, out_len - used, "\n\r Error command !\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Trigger CLM: echo clm 1\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Get CLM results: echo clm 2\n"));
		}
	}
		break;
	
	case PHYDM_BB_INFO:
	{
		s4Byte value32 = 0;
		
		phydm_BB_Debug_Info(pDM_Odm, &used, output, &out_len);

		if (pDM_Odm->SupportICType & ODM_RTL8822B && input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			ODM_SetBBReg(pDM_Odm, 0x1988, 0x003fff00, var1[0]);
			value32 = ODM_GetBBReg(pDM_Odm, 0xf84, bMaskDWord);
			value32 = (value32 & 0xff000000)>>24;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = condition num = %d, subcarriers = %d\n", "Over condition num subcarrier", var1[0], value32));
			ODM_SetBBReg(pDM_Odm, 0x1988 , BIT22, 0x0);	/*disable report condition number*/
		}
	}
		break;

	case PHYDM_TXBF:
	{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
#if (BEAMFORMING_SUPPORT == 1)
		PRT_BEAMFORMING_INFO	pBeamformingInfo = &pDM_Odm->BeamformingInfo;

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
		if (var1[0] == 1) {
			pBeamformingInfo->applyVmatrix = TRUE;
			pBeamformingInfo->snding3SS = FALSE;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n apply V matrix and 3SS 789 dont snding\n"));
		} else if (var1[0] == 0) {
			pBeamformingInfo->applyVmatrix = FALSE;
			pBeamformingInfo->snding3SS = TRUE;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n dont apply V matrix and 3SS 789 snding\n"));
		} else
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n unknown cmd!!\n"));
#else
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n no TxBF !!\n"));
#endif
#endif
	}
		break;
		
	case PHYDM_PAUSE_DIG_EN:
		

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}
				
		if (input_idx >= 1) {
			if (var1[0] == 0) {
				odm_PauseDIG(pDM_Odm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_7, (u1Byte)var1[1]);
				PHYDM_SNPRINTF((output + used, out_len - used, "Set IGI_value = ((%x))\n", var1[1]));
			} else if (var1[0] == 1) {
				odm_PauseDIG(pDM_Odm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_7, (u1Byte)var1[1]);
				PHYDM_SNPRINTF((output + used, out_len - used, "Resume IGI_value\n"));
			} else 
				PHYDM_SNPRINTF((output + used, out_len - used, "echo  (1:pause, 2resume)  (IGI_value)\n"));
		}
		
		break;
		
	case PHYDM_H2C:

		for (i = 0; i < 8; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1)
			phydm_h2C_debug(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);

		
		break;
		
	case PHYDM_ANT_SWITCH:

		for (i = 0; i < 8; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			
			#if (RTL8821A_SUPPORT == 1)
			phydm_set_ext_switch(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
			#else
			PHYDM_SNPRINTF((output + used, out_len - used, "Not Support IC"));
			#endif
		}

		
		break;		

	default:
		PHYDM_SNPRINTF((output + used, out_len - used, "SET, unknown command!\n"));
		break;

	}
}


VOID
phydm_la_mode_bb_setting(
	IN	PVOID		pDM_VOID,
	IN	u4Byte		DbgPort,
	IN	BOOLEAN		bTriggerEdge,
	IN	u1Byte		sampling_rate
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	/*u4Byte			dword;*/

	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {
		ODM_SetBBReg(pDM_Odm, 0x198C , BIT2|BIT1|BIT0, 7); /*disable dbg clk gating*/

		/*dword= ODM_GetBBReg(pDM_Odm, 0x8FC, bMaskDWord);*/
		/*DbgPrint("dbg_port = ((0x%x))\n", dword);*/
		ODM_SetBBReg(pDM_Odm, 0x8FC, bMaskDWord, DbgPort);
		ODM_SetBBReg(pDM_Odm, 0x95C , BIT31, bTriggerEdge); /*0: posedge, 1: negedge*/
		ODM_SetBBReg(pDM_Odm, 0x95c, 0xe0, sampling_rate);
		/*	(0:) '80MHz'
			(1:) '40MHz'
			(2:) '20MHz'
			(3:) '10MHz'
			(4:) '5MHz'
			(5:) '2.5MHz'
			(6:) '1.25MHz'
			(7:) '160MHz (for BW160 ic)'
		*/
	} else {
		ODM_SetBBReg(pDM_Odm, 0x908, bMaskDWord, DbgPort);
		ODM_SetBBReg(pDM_Odm, 0x9A0 , BIT31, bTriggerEdge); /*0: posedge, 1: negedge*/
		ODM_SetBBReg(pDM_Odm, 0x9A0, 0xe0, sampling_rate);
		/*	(0:) '80MHz'
			(1:) '40MHz'
			(2:) '20MHz'
			(3:) '10MHz'
			(4:) '5MHz'
			(5:) '2.5MHz'
			(6:) '1.25MHz'
			(7:) '160MHz (for BW160 ic)'
		*/
	}
}

u1Byte
phydm_la_mode_mac_setting(
	IN	PVOID		pDM_VOID,
	IN	u4Byte		TriggerTime_mu_sec
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			TriggerTime_unit_num;
	u4Byte			time_unit = 0;

	if (TriggerTime_mu_sec < 128) {
		time_unit = 0; /*unit: 1mu sec*/
	} else if (TriggerTime_mu_sec < 256) {
		time_unit = 1; /*unit: 2mu sec*/	
	} else if (TriggerTime_mu_sec < 512) {
		time_unit = 2; /*unit: 4mu sec*/	
	} else if (TriggerTime_mu_sec < 1024) {
		time_unit = 3; /*unit: 8mu sec*/	
	} else if (TriggerTime_mu_sec < 2048) {
		time_unit = 4; /*unit: 16mu sec*/	
	} else if (TriggerTime_mu_sec < 4096) {
		time_unit = 5; /*unit: 32mu sec*/	
	} else if (TriggerTime_mu_sec < 8192) {
		time_unit = 6; /*unit: 64mu sec*/	
	}
	
	TriggerTime_unit_num = (u1Byte)(TriggerTime_mu_sec>>time_unit);
	pDM_Odm->ADCsmp_time_unit = time_unit;
	DbgPrint("time_unit = ((0x%x))\n", time_unit);
	
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {
		ODM_SetMACReg(pDM_Odm, 0x7cc , BIT20|BIT19|BIT18, time_unit);
	} else {

	}
	return (TriggerTime_unit_num & 0x7f);
	
}

#ifdef __ECOS
char *strsep(char **s, const char *ct)
{
	char *sbegin = *s;
	char *end;

	if (sbegin == NULL)
		return NULL;

	end = strpbrk(sbegin, ct);
	if (end)
		*end++ = '\0';
	*s = end;
	return sbegin;
}
#endif

#if(DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_AP))
s4Byte
phydm_cmd(
	IN PDM_ODM_T	pDM_Odm,
	IN char		*input,
	IN u4Byte	in_len,
	IN u1Byte	flag,
	OUT char	*output,
	IN u4Byte	out_len
)
{
	char *token;
	u4Byte	Argc = 0;
	char		Argv[MAX_ARGC][MAX_ARGV];

	do {
		token = strsep(&input, ", ");
		if (token) {
			strcpy(Argv[Argc], token);
			Argc++;
		} else
			break;
	} while (Argc < MAX_ARGC);

	if (Argc == 1)
		Argv[0][strlen(Argv[0]) - 1] = '\0';

	phydm_cmd_parser(pDM_Odm, Argv, Argc, flag, output, out_len);

	return 0;
}
#endif


VOID
phydm_fw_trace_handler(
	IN	PVOID	pDM_VOID,
	IN	pu1Byte	CmdBuf,
	IN	u1Byte	CmdLen
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	/*u1Byte	debug_trace_11byte[60];*/
	u1Byte		freg_num, c2h_seq, buf_0 = 0;

	if (!(pDM_Odm->SupportICType & PHYDM_IC_3081_SERIES))
		return;

	if (CmdLen > 12)
		return;
	
	buf_0 = CmdBuf[0];
	freg_num = (buf_0 & 0xf);
	c2h_seq = (buf_0 & 0xf0) >> 4;
	/*ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW debug message] freg_num = (( %d )), c2h_seq = (( %d ))\n", freg_num,c2h_seq ));*/

	/*strncpy(debug_trace_11byte,&CmdBuf[1],(CmdLen-1));*/
	/*debug_trace_11byte[CmdLen-1] = '\0';*/
	/*ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW debug message] %s\n", debug_trace_11byte));*/
	/*ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW debug message] CmdLen = (( %d ))\n", CmdLen));*/
	/*ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW debug message] c2h_cmd_start  = (( %d ))\n", pDM_Odm->c2h_cmd_start));*/



	/*ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("pre_seq = (( %d )), current_seq = (( %d ))\n", pDM_Odm->pre_c2h_seq, c2h_seq));*/
	/*ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("fw_buff_is_enpty = (( %d ))\n", pDM_Odm->fw_buff_is_enpty));*/

	if ((c2h_seq != pDM_Odm->pre_c2h_seq)  &&  pDM_Odm->fw_buff_is_enpty == FALSE) {
		pDM_Odm->fw_debug_trace[pDM_Odm->c2h_cmd_start] = '\0';
		ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW Dbg Queue Overflow] %s\n", pDM_Odm->fw_debug_trace));
		pDM_Odm->c2h_cmd_start = 0;
	}

	if ((CmdLen - 1) > (60 - pDM_Odm->c2h_cmd_start)) {
		pDM_Odm->fw_debug_trace[pDM_Odm->c2h_cmd_start] = '\0';
		ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW Dbg Queue error: wrong C2H length] %s\n", pDM_Odm->fw_debug_trace));
		pDM_Odm->c2h_cmd_start = 0;
		return;
	}

	strncpy((char *)&(pDM_Odm->fw_debug_trace[pDM_Odm->c2h_cmd_start]), (char *)&CmdBuf[1], (CmdLen-1));
	pDM_Odm->c2h_cmd_start += (CmdLen - 1);
	pDM_Odm->fw_buff_is_enpty = FALSE;	
	
	if (freg_num == 0 || pDM_Odm->c2h_cmd_start >= 60) {
		if (pDM_Odm->c2h_cmd_start < 60)
			pDM_Odm->fw_debug_trace[pDM_Odm->c2h_cmd_start] = '\0';
		else
			pDM_Odm->fw_debug_trace[59] = '\0';

		ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW DBG Msg] %s\n", pDM_Odm->fw_debug_trace));
		/*DbgPrint("[FW DBG Msg] %s\n", pDM_Odm->fw_debug_trace);*/
		pDM_Odm->c2h_cmd_start = 0;
		pDM_Odm->fw_buff_is_enpty = TRUE;
	}

	pDM_Odm->pre_c2h_seq = c2h_seq;
}

VOID
phydm_fw_trace_handler_code(
	IN	PVOID	pDM_VOID,
	IN	pu1Byte	Buffer,
	IN	u1Byte	CmdLen
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	function = Buffer[0];
	u1Byte	dbg_num = Buffer[1];
	u2Byte	content_0 = (((u2Byte)Buffer[3])<<8)|((u2Byte)Buffer[2]);
	u2Byte	content_1 = (((u2Byte)Buffer[5])<<8)|((u2Byte)Buffer[4]);		
	u2Byte	content_2 = (((u2Byte)Buffer[7])<<8)|((u2Byte)Buffer[6]);	
	u2Byte	content_3 = (((u2Byte)Buffer[9])<<8)|((u2Byte)Buffer[8]);
	u2Byte	content_4 = (((u2Byte)Buffer[11])<<8)|((u2Byte)Buffer[10]);

	if(CmdLen >12) {
		ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW Msg] Invalid cmd length (( %d )) >12 \n", CmdLen));
	}
	
	//ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW Msg] Func=((%d)),  num=((%d)), ct_0=((%d)), ct_1=((%d)), ct_2=((%d)), ct_3=((%d)), ct_4=((%d))\n", 
	//	function, dbg_num, content_0, content_1, content_2, content_3, content_4));
	
	/*--------------------------------------------*/
#if (CONFIG_RA_FW_DBG_CODE)
	if(function == RATE_DECISION) {
		if(dbg_num == 0) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] RA_CNT=((%d))  Max_device=((%d))--------------------------->\n", content_1, content_2));
			} else if(content_0 == 2) {
				 ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Check RA macid= ((%d)), MediaStatus=((%d)), Dis_RA=((%d)),  try_bit=((0x%x))\n", content_1, content_2, content_3, content_4));
			} else if(content_0 == 3) {
				 ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Check RA  total=((%d)),  drop=((0x%x)), TXRPT_TRY_bit=((%x)), bNoisy=((%x))\n", content_1, content_2, content_3, content_4));
			}
		} else if(dbg_num == 1) {
			if (content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] RTY[0,1,2,3]=[ %d , %d , %d , %d ]\n", content_1, content_2, content_3, content_4));
			} else if (content_0 == 2) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] RTY[4]=[ %d ], drop=(( %d )), total=(( %d )), current_rate=((0x %x ))", content_1, content_2, content_3, content_4));
				phydm_print_rate(pDM_Odm, (u1Byte)content_4, ODM_FW_DEBUG_TRACE);
			} else if (content_0 == 3) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] penality_idx=(( %d ))\n", content_1));
			} else if (content_0 == 4) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] RSSI=(( %d )), ra_stage = (( %d ))\n", content_1, content_2));
			}
		}
		
		else if(dbg_num == 3) {
			if (content_0 == 1)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Fast_RA (( DOWN ))  total=((%d)),  total>>1=((%d)), R4+R3+R2 = ((%d)), RateDownHold = ((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 2)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Fast_RA (( UP ))  total_acc=((%d)),  total_acc>>1=((%d)), R4+R3+R2 = ((%d)), RateDownHold = ((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 3)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Fast_RA (( UP )) ((Rate Down Hold))  RA_CNT=((%d))\n", content_1));
			else if (content_0 == 4)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Fast_RA (( UP )) ((tota_accl<5 skip))  RA_CNT=((%d))\n", content_1));
			else if (content_0 == 8)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Fast_RA (( Reset Tx Rpt )) RA_CNT=((%d))\n", content_1));
		}

		else if (dbg_num == 4) {
			if (content_0 == 3) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] RER_CNT   PCR_ori =(( %d )),  ratio_ori =(( %d )), pcr_updown_bitmap =(( 0x%x )), pcr_var_diff =(( %d ))\n", content_1, content_2, content_3, content_4));
				/**/
			} else if (content_0 == 4) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] pcr_shift_value =(( %s%d )), rate_down_threshold =(( %d )), rate_up_threshold =(( %d ))\n", ((content_1) ? "+" : "-"), content_2, content_3, content_4));
				/**/
			} else if (content_0 == 5) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] pcr_mean =(( %d )), PCR_VAR =(( %d )), offset =(( %d )), decision_offset_p =(( %d ))\n", content_1, content_2, content_3, content_4));
				/**/
			}
		}
		
		else if(dbg_num == 5) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] (( UP))  Nsc=(( %d )), N_High=(( %d )), RateUp_Waiting=(( %d )), RateUp_Fail=(( %d ))\n", content_1, content_2, content_3, content_4));
			} else if(content_0 == 2) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] ((DOWN))  Nsc=(( %d )), N_Low=(( %d ))\n", content_1, content_2));
			} else if(content_0 == 3) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] ((HOLD))  Nsc=((%d)), N_High=((%d)), N_Low=((%d)), Reset_CNT=((%d))\n", content_1, content_2, content_3, content_4));
			}
		}
		else if(dbg_num == 0x60) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] ((AP RPT))  macid=((%d)), BUPDATE[macid]=((%d))\n", content_1, content_2));
			} else if(content_0 == 4) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] ((AP RPT))  pass=((%d)), rty_num=((%d)), drop=((%d)), total=((%d))\n", content_1, content_2, content_3, content_4));
			} else if(content_0 == 5) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] ((AP RPT))  PASS=((%d)), RTY_NUM=((%d)), DROP=((%d)), TOTAL=((%d))\n", content_1, content_2, content_3, content_4));
			}
		}
	} 
	/*--------------------------------------------*/
	else if (function == INIT_RA_TABLE){
		if(dbg_num == 3) {
			ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][INIT_RA_INFO] Ra_init, RA_SKIP_CNT = (( %d ))\n", content_0));
		}
		
	} 
	/*--------------------------------------------*/
	else if (function == RATE_UP) {
		if(dbg_num == 2) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateUp]  ((Highest rate -> return)), macid=((%d))  Nsc=((%d))\n", content_1, content_2));
			}
		} else if(dbg_num == 5) {
			if (content_0 == 0)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateUp]  ((Rate UP)), up_rate_tmp=((0x%x)), rate_idx=((0x%x)), SGI_en=((%d)),  SGI=((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 1)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateUp]  ((Rate UP)), rate_1=((0x%x)), rate_2=((0x%x)), BW=((%d)), Try_Bit=((%d))\n", content_1, content_2, content_3, content_4));
		}
		
	} 
	/*--------------------------------------------*/
	else if (function == RATE_DOWN) {
		 if(dbg_num == 5) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateDownStep]  ((Rate Down)), macid=((%d)), rate1=((0x%x)),  rate2=((0x%x)), BW=((%d))\n", content_1, content_2, content_3, content_4));
			}
		}
	} else if (function == TRY_DONE) {
		if (dbg_num == 1) {
			if (content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][Try Done]  ((try succsess )) macid=((%d)), Try_Done_cnt=((%d))\n", content_1, content_2));
				/**/
			}
		} else if (dbg_num == 2) {
			if (content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][Try Done]  ((try)) macid=((%d)), Try_Done_cnt=((%d)),  rate_2=((%d)),  try_succes=((%d))\n", content_1, content_2, content_3, content_4));
				/**/
			}
		}
	} 
	/*--------------------------------------------*/
	else if (function == RA_H2C) {
		if (dbg_num == 1) {
			if (content_0 == 0) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][H2C=0x49]  fw_trace_en=((%d)), mode =((%d)),  macid=((%d))\n", content_1, content_2, content_3));
				/**/
				/*C2H_RA_Dbg_code(F_RA_H2C,1,0, SysMib.ODM.DEBUG.fw_trace_en, mode, macid , 0);    //RA MASK*/
			}
			#if 0
			else if (dbg_num == 2) {
			
				if (content_0 == 1) {
					ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][H2C=0x40]  MACID=((%d)), Rate ID=((%d)),  SGI=((%d)),  BW=((%d))\n", content_1, content_2, content_3, content_4));
					/**/
				} else if (content_0 == 2) {
					ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][H2C=0x40]   VHT_en=((%d)), Disable_PowerTraining=((%d)),  Disable_RA=((%d)),  No_Update=((%d))\n", content_1, content_2, content_3, content_4));
					/**/
				} else if (content_0 == 3) {
					ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][H2C=0x40]   RA_MSK=[%x | %x | %x | %x ]\n", content_1, content_2, content_3, content_4));
					/**/
				}
			}
			#endif
		}
	}
	/*--------------------------------------------*/
	else if (function == F_RATE_AP_RPT) {
		 if(dbg_num == 1) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  ((1)), SPE_STATIS=((0x%x))---------->\n", content_3));				
			} 
		} else if(dbg_num == 2) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  RTY_all=((%d))\n", content_1));				
			} 
		} else if(dbg_num == 3) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  MACID1[%d], TOTAL=((%d)),  RTY=((%d))\n", content_3, content_1, content_2));
			} 
		} else if(dbg_num == 4) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  MACID2[%d], TOTAL=((%d)),  RTY=((%d))\n", content_3, content_1, content_2));
			} 
		} else if(dbg_num == 5) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  MACID1[%d], PASS=((%d)),  DROP=((%d))\n", content_3, content_1, content_2));
			} 
		} else if(dbg_num == 6) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  MACID2[%d],, PASS=((%d)),  DROP=((%d))\n", content_3, content_1, content_2));
			} 
		}
	} else {
		ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][general][%d, %d, %d] = {%d, %d, %d, %d}\n", function, dbg_num, content_0, content_1, content_2, content_3, content_4));
		/**/
	}
#else
	ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][general][%d, %d, %d] = {%d, %d, %d, %d}\n", function, dbg_num, content_0, content_1, content_2, content_3, content_4));
#endif
	/*--------------------------------------------*/
		

}

VOID
phydm_fw_trace_handler_8051(
	IN	PVOID	pDM_VOID,
	IN	pu1Byte	Buffer,
	IN	u1Byte	CmdLen
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
#if 0
	if (CmdLen >= 3)
		CmdBuf[CmdLen - 1] = '\0';
	ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW DBG Msg] %s\n", &(CmdBuf[3])));
#else

	int i = 0;
	u1Byte	Extend_c2hSubID = 0, Extend_c2hDbgLen = 0, Extend_c2hDbgSeq = 0;
	u1Byte	fw_debug_trace[128];
	pu1Byte	Extend_c2hDbgContent = 0;

	if (CmdLen > 127)
		return;

	Extend_c2hSubID = Buffer[0];
	Extend_c2hDbgLen = Buffer[1];
	Extend_c2hDbgContent = Buffer + 2; /*DbgSeq+DbgContent  for show HEX*/

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	RT_DISP(FC2H, C2H_Summary, ("[Extend C2H packet], Extend_c2hSubId=0x%x, Extend_c2hDbgLen=%d\n", 
			Extend_c2hSubID, Extend_c2hDbgLen));
	
	RT_DISP_DATA(FC2H, C2H_Summary, "[Extend C2H packet], Content Hex:", Extend_c2hDbgContent, CmdLen-2);
	#endif

GoBackforAggreDbgPkt:
	i = 0;
	Extend_c2hDbgSeq = Buffer[2];
	Extend_c2hDbgContent = Buffer + 3;
	
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	RT_DISP(FC2H, C2H_Summary, ("[RTKFW, SEQ= %d] :", Extend_c2hDbgSeq));
	#endif	

	for (; ; i++) {
		fw_debug_trace[i] = Extend_c2hDbgContent[i];
		if (Extend_c2hDbgContent[i + 1] == '\0') {
			fw_debug_trace[i + 1] = '\0';
			ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW DBG Msg] %s", &(fw_debug_trace[0])));
			break;
		} else if (Extend_c2hDbgContent[i] == '\n') {
			fw_debug_trace[i + 1] = '\0';
			ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW DBG Msg] %s", &(fw_debug_trace[0])));
			Buffer = Extend_c2hDbgContent + i + 3;
			goto GoBackforAggreDbgPkt;
		}
	}


#endif
}


