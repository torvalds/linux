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

#include "Mp_Precomp.h"
#include "phydm_precomp.h"


VOID
PHYDM_InitDebugSetting(
	IN		PDM_ODM_T		pDM_Odm
)
{
	pDM_Odm->DebugLevel = ODM_DBG_TRACE;

	pDM_Odm->DebugComponents			=
		\
#if DBG
//BB Functions
//									ODM_COMP_DIG					|
//									ODM_COMP_RA_MASK				|
//									ODM_COMP_DYNAMIC_TXPWR		|
//									ODM_COMP_FA_CNT				|
//									ODM_COMP_RSSI_MONITOR			|
//									ODM_COMP_CCK_PD				|
//									ODM_COMP_ANT_DIV				|
//									ODM_COMP_PWR_SAVE				|
//									ODM_COMP_PWR_TRAIN			|
//									ODM_COMP_RATE_ADAPTIVE		|
//									ODM_COMP_PATH_DIV				|
//									ODM_COMP_DYNAMIC_PRICCA		|
//									ODM_COMP_RXHP					|
//									ODM_COMP_MP 					|
//									ODM_COMP_CFO_TRACKING		|
//									ODM_COMP_ACS					|
//									PHYDM_COMP_ADAPTIVITY			|
//									PHYDM_COMP_RA_DBG				|
/*									PHYDM_COMP_TXBF					|*/
//MAC Functions
//									ODM_COMP_EDCA_TURBO			|
//									ODM_COMP_EARLY_MODE			|
/*									ODM_FW_DEBUG_TRACE				|*/
//RF Functions
//									ODM_COMP_TX_PWR_TRACK		|
//									ODM_COMP_RX_GAIN_TRACK		|
//									ODM_COMP_CALIBRATION			|
//Common
//									ODM_COMP_COMMON				|
//									ODM_COMP_INIT					|
//									ODM_COMP_PSD					|
#endif
		0;

	pDM_Odm->fw_buff_is_enpty = TRUE;
	pDM_Odm->pre_c2h_seq = 0;
}

#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
static u1Byte	BbDbgBuf[BB_TMP_BUF_SIZE];

VOID
phydm_BB_RxHang_Info(IN PDM_ODM_T pDM_Odm)
{
	u4Byte	value32 = 0;


	if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		return;

	value32 = ODM_GetBBReg(pDM_Odm, 0xF80 , bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rptreg of sc/bw/ht/...", value32);
	DCMD_Printf(BbDbgBuf);

	/* dbg_port = state machine */
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x007);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "state machine", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = CCA-related*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x204);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "CCA-related", (value32));
		DCMD_Printf(BbDbgBuf);
	}


	/* dbg_port = edcca/rxd*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x278);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "edcca/rxd", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = rx_state/mux_state/ADC_MASK_OFDM*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x290);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rx_state/mux_state/ADC_MASK_OFDM", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = bf-related*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x2B2);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "bf-related", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = bf-related*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x2B8);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "bf-related", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = txon/rxd*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xA03);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "txon/rxd", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = l_rate/l_length*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xA0B);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "l_rate/l_length", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = rxd/rxd_hit*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xA0D);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rxd/rxd_hit", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = dis_cca*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAA0);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "dis_cca", (value32));
		DCMD_Printf(BbDbgBuf);
	}


	/* dbg_port = tx*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAB0);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "tx", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = rx plcp*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD0);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rx plcp", (value32));
		DCMD_Printf(BbDbgBuf);

		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD1);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rx plcp", (value32));
		DCMD_Printf(BbDbgBuf);

		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD2);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rx plcp", (value32));
		DCMD_Printf(BbDbgBuf);

		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD3);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rx plcp", (value32));
		DCMD_Printf(BbDbgBuf);
	}

}

VOID
phydm_BB_Debug_Info(IN PDM_ODM_T pDM_Odm)
{

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
	const char *RXHT_table[3] = {"legacy", "HT", "VHT"};
	const char *BW_table[3] = {"20M", "40M", "80M"};
	const char *RXSC_table[7] = {"duplicate/full bw", "usc20-1", "lsc20-1", "usc20-2", "lsc20-2",  "usc40", "lsc40"};

	const char *L_rate[8] = {"6M", "9M", "12M", "18M", "24M", "36M", "48M", "54M"};


	/*
	const double evm_comp_20M = 0.579919469776867; //10*log10(64.0/56.0)
	const double evm_comp_40M = 0.503051183113957; //10*log10(128.0/114.0)
	const double evm_comp_80M = 0.244245993314183; //10*log10(256.0/242.0)
	const double evm_comp_160M = 0.244245993314183; //10*log10(512.0/484.0)
	   */

	if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		return;

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s\n", "BB Report Info");
	DCMD_Printf(BbDbgBuf);

	/*BW & Mode Detection*/
	
	value32 = ODM_GetBBReg(pDM_Odm, 0xf80 , bMaskDWord);
	value32_2 = value32;
	RX_HT_BW = (u1Byte)(value32 & 0x1);
	RX_VHT_BW = (u1Byte)((value32 >> 1) & 0x3);
	RXSC = (u1Byte)(value32 & 0x78);
	value32_1 = (value32 & 0x180) >> 7;
	RX_HT = (u1Byte)(value32_1);
	/*
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "F80", value32_2);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_HT_BW", RX_HT_BW);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_VHT_BW", RX_VHT_BW);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_SC", RXSC);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_HT", RX_HT);
	DCMD_Printf(BbDbgBuf);
	*/

	//rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  RX_HT:%s ", RXHT_table[RX_HT]);
	//DCMD_Printf(BbDbgBuf);
	RX_BW = 0;

	if (RX_HT == 2) {
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  Mode: VHT Mode");
		DCMD_Printf(BbDbgBuf);
		if (RX_VHT_BW == 0) {
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=20M");
			DCMD_Printf(BbDbgBuf);
		} else if (RX_VHT_BW == 1) {
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=40M");
			DCMD_Printf(BbDbgBuf);
		} else {
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=80M");
			DCMD_Printf(BbDbgBuf);
		}
		RX_BW = RX_VHT_BW;
	} else if (RX_HT == 1) {
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  Mode: HT Mode");
		DCMD_Printf(BbDbgBuf);
		if (RX_HT_BW == 0) {
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=20M");
			DCMD_Printf(BbDbgBuf);
		} else if (RX_HT_BW == 1) {
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=40M");
			DCMD_Printf(BbDbgBuf);
		}
		RX_BW = RX_HT_BW;
	} else {
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  Mode: Legeacy Mode");
		DCMD_Printf(BbDbgBuf);
	}

	if (RX_HT != 0) {
		if (RXSC == 0)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  duplicate/full bw");
		else if (RXSC == 1)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  usc20-1");
		else if (RXSC == 2)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  lsc20-1");
		else if (RXSC == 3)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  usc20-2");
		else if (RXSC == 4)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  lsc20-2");
		else if (RXSC == 9)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  usc40");
		else if (RXSC == 10)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  lsc40");
		DCMD_Printf(BbDbgBuf);
	}
	/*
	if(RX_HT == 2){
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "  BW:%s", BW_table[RX_VHT_BW]);
		RX_BW = RX_VHT_BW;
		}
	else if(RX_HT == 1){
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "  BW:%s", BW_table[RX_HT_BW]);
		RX_BW = RX_HT_BW;
		}
	else
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE,  "");

	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "  RXSC:%s", RXSC_table[RXSC]);
	DCMD_Printf(BbDbgBuf);
	*/


//	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "dB Conversion: 10log(65)", ODM_PWdB_Conversion(65,10,0));
//	DCMD_Printf(BbDbgBuf);

	/* RX signal power and AGC related info*/

	value32 = ODM_GetBBReg(pDM_Odm, 0xF90 , bMaskDWord);
	pwDB = (u1Byte)((value32 & bMaskByte1) >> 8);
	pwDB = pwDB >> 1;
	sig_power = -110 + pwDB;
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "OFDM RX Signal Power(dB)", sig_power);
	DCMD_Printf(BbDbgBuf);


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
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "OFDM RX RF Gain(A/B/C/D)", RF_gain_pathA, RF_gain_pathA, RF_gain_pathC, RF_gain_pathD);
	DCMD_Printf(BbDbgBuf);


	/* RX Counter related info*/

	value32 = ODM_GetBBReg(pDM_Odm, 0xF08 , bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "OFDM CCA Counter", ((value32 & 0xFFFF0000) >> 16));
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xFD0 , bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "OFDM SBD Fail Counter", value32 & 0xFFFF);
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xFC4 , bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "VHT SIGA/SIGB CRC8 Fail Counter", value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16));
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xFCC , bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "CCK CCA Counter", value32 & 0xFFFF);
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xFBC , bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "LSIG (\"Parity Fail\"/\"Rate Illegal\") Counter", value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16));
	DCMD_Printf(BbDbgBuf);

	value32_1 = ODM_GetBBReg(pDM_Odm, 0xFC8 , bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xFC0 , bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "HT/VHT MCS NOT SUPPORT counter", ((value32_2 & 0xFFFF0000) >> 16), value32_1 & 0xFFFF);
	DCMD_Printf(BbDbgBuf);


	/* PostFFT related info*/


	value32 = ODM_GetBBReg(pDM_Odm, 0xF8c , bMaskDWord);
	RXEVM_0 = (s1Byte)((value32 & bMaskByte2) >> 16);
	RXEVM_0 /= 2;
	if (RXEVM_0 < -63)
		RXEVM_0 = 0;

	DCMD_Printf(BbDbgBuf);
	RXEVM_1 = (s1Byte)((value32 & bMaskByte3) >> 24);
	RXEVM_1 /= 2;
	value32 = ODM_GetBBReg(pDM_Odm, 0xF88 , bMaskDWord);
	RXEVM_2 = (s1Byte)((value32 & bMaskByte2) >> 16);
	RXEVM_2 /= 2;

	if (RXEVM_1 < -63)
		RXEVM_1 = 0;
	if (RXEVM_2 < -63)
		RXEVM_2 = 0;

	/*
	if(RX_BW == 0){
		RXEVM_0 -= evm_comp_20M;
		RXEVM_1 -= evm_comp_20M;
		RXEVM_2 -= evm_comp_20M;
		}
	else if(RX_BW == 1){
		RXEVM_0 -= evm_comp_40M;
		RXEVM_1 -= evm_comp_40M;
		RXEVM_2 -= evm_comp_40M;
		}
	else if (RX_BW == 2){
		RXEVM_0 -= evm_comp_80M;
		RXEVM_1 -= evm_comp_80M;
		RXEVM_2 -= evm_comp_80M;
		}
		*/
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d", "RXEVM (1ss/2ss/3ss)", RXEVM_0, RXEVM_1, RXEVM_2);
	DCMD_Printf(BbDbgBuf);

//	value32 = ODM_GetBBReg(pDM_Odm, 0xD14 ,bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D, dB)", RX_SNR_pathA, RX_SNR_pathB, RX_SNR_pathC, RX_SNR_pathD);
	DCMD_Printf(BbDbgBuf);
//	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "B_RXSNR", (value32&0xFF00)>>9);
//	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xF8C , bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "CSI_1st /CSI_2nd", value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16));
	DCMD_Printf(BbDbgBuf);


	//BW & Mode Detection

	//Reset Page F Counter
	ODM_SetBBReg(pDM_Odm, 0xB58 , BIT0, 1);
	ODM_SetBBReg(pDM_Odm, 0xB58 , BIT0, 0);

	//CFO Report Info
	//Short CFO
	value32 = ODM_GetBBReg(pDM_Odm, 0xd0c , bMaskDWord);
	value32_1 = ODM_GetBBReg(pDM_Odm, 0xd4c , bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xd8c , bMaskDWord);
	value32_3 = ODM_GetBBReg(pDM_Odm, 0xdcc , bMaskDWord);

	SFO_A = (s4Byte)(value32 & bMask12Bits);
	SFO_B = (s4Byte)(value32_1 & bMask12Bits);
	SFO_C = (s4Byte)(value32_2 & bMask12Bits);
	SFO_D = (s4Byte)(value32_3 & bMask12Bits);

	LFO_A = (s4Byte)(value32 >> 16);
	LFO_B = (s4Byte)(value32_1 >> 16);
	LFO_C = (s4Byte)(value32_2 >> 16);
	LFO_D = (s4Byte)(value32_3 >> 16);

	//SFO 2's to dec
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

	//LFO 2's to dec

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
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "CFO Report Info");
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  Short CFO(Hz) <A/B/C/D>", SFO_A, SFO_B, SFO_C, SFO_D);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  Long CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D);
	DCMD_Printf(BbDbgBuf);

	//SCFO
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

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  Value SCFO(Hz) <A/B/C/D>", SFO_A, SFO_B, SFO_C, SFO_D);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  ACQ CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D);
	DCMD_Printf(BbDbgBuf);

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

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  End CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D);
	DCMD_Printf(BbDbgBuf);

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

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "L-SIG");
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n   Rate:%s", L_rate[idx]);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x/ %x /%x", "  Rsv/Length/Parity", rsv, RX_BW, Length);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "HT-SIG1");
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xf2c , bMaskDWord);  /*HT SIG*/
	if (RX_HT == 1) {

		HMCSS = (u1Byte)(value32 & 0x7F);
		HRX_BW = (u1Byte)(value32 & 0x80);
		HLength = (u2Byte)((value32 >> 8) & 0xffff);
	}
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x", "  MCS/BW/Length", HMCSS, HRX_BW, HLength);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "HT-SIG2");
	DCMD_Printf(BbDbgBuf);

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
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/ %x/ %x", "  Smooth/NoSound/Rsv/Aggregate/STBC/LDPC", smooth, htsound, rsv, agg, stbc, fec);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x", "  SGI/E-HT-LTFs/CRC/Tail", sgi, htltf, htcrc8, Tail);
	DCMD_Printf(BbDbgBuf);


	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "VHT-SIG-A1");
	DCMD_Printf(BbDbgBuf);
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

	//rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x", "F2C", value32);
	//DCMD_Printf(BbDbgBuf);


	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/ %x/ %x /%x /%x", "  BW/Rsv1/STBC/GID/Nsts/PAID/TXOPPS/Rsv2", vRX_BW, vrsv, vstbc, vgid, vNsts, vpaid, vtxops, vrsv2);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "VHT-SIG-A2");
	DCMD_Printf(BbDbgBuf);
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
	/*rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x", "F30", value32);*/
	/*DCMD_Printf(BbDbgBuf);*/

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/ %x/ %x/ %x", "  SGI/FEC/MCS/BF/Rsv/CRC/Tail", sgiext, fecext, vMCSS, bf, vrsv, vhtcrc8, vTail);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "VHT-SIG-B");
	DCMD_Printf(BbDbgBuf);
	value32 = ODM_GetBBReg(pDM_Odm, 0xf34 , bMaskDWord);  /*VHT SIG*/
	{
		vLength = (u2Byte)(value32 & 0x1fffff);
		vbrsv = (u1Byte)((value32 & 0x600000) >> 20);
		vbTail = (u2Byte)((value32 & 0x1f800000) >> 20);
		vbcrc = (u1Byte)((value32 & 0x80000000) >> 28);

	}
	/*rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x", "F34", value32);*/
	/*DCMD_Printf(BbDbgBuf);*/
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/", "  Length/Rsv/Tail/CRC", vLength, vbrsv, vbTail, vbcrc);
	DCMD_Printf(BbDbgBuf);


}

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
	pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_BasicDbgMsg==>\n"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("bLinked = %d, RSSI_Min = %d, CurrentIGI = 0x%x\n",
				 pDM_Odm->bLinked, pDM_Odm->RSSI_Min, pDM_DigTable->CurIGValue));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("Cnt_Cck_fail = %d, Cnt_Ofdm_fail = %d, Total False Alarm = %d\n",
				 FalseAlmCnt->Cnt_Cck_fail, FalseAlmCnt->Cnt_Ofdm_fail, FalseAlmCnt->Cnt_all));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("RxRate = 0x%x, RSSI_A = %d, RSSI_B = %d\n",
				 pDM_Odm->RxRate, pDM_Odm->RSSI_A, pDM_Odm->RSSI_B));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("RSSI_C = %d, RSSI_D = %d\n", pDM_Odm->RSSI_C, pDM_Odm->RSSI_D));
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

	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "% Basic Profile %"));

	if (pDM_Odm->SupportICType == ODM_RTL8192C)			
		ICType = "RTL8192C";
	else if (pDM_Odm->SupportICType == ODM_RTL8192D)
		ICType = "RTL8192D";
	else if (pDM_Odm->SupportICType == ODM_RTL8723A)
		ICType = "RTL8723A";
	else if (pDM_Odm->SupportICType == ODM_RTL8188E)
		ICType = "RTL8188E";
	else if (pDM_Odm->SupportICType == ODM_RTL8812)
		ICType = "RTL8812A";
	else if (pDM_Odm->SupportICType == ODM_RTL8821)
		ICType = "RTL8821A";
	else if (pDM_Odm->SupportICType == ODM_RTL8192E)
		ICType = "RTL8192E";
	else if (pDM_Odm->SupportICType == ODM_RTL8723B)
		ICType = "RTL8723B";
	else if (pDM_Odm->SupportICType == ODM_RTL8814A)
		ICType = "RTL8814A";
	else if (pDM_Odm->SupportICType == ODM_RTL8881A)
		ICType = "RTL8881A";
	else if (pDM_Odm->SupportICType == ODM_RTL8821B)
		ICType = "RTL8821B";
	else if (pDM_Odm->SupportICType == ODM_RTL8822B)
		ICType = "RTL8822B";
	else if (pDM_Odm->SupportICType == ODM_RTL8703B)
		ICType = "RTL8703B";
	else if (pDM_Odm->SupportICType == ODM_RTL8195A)
		ICType = "RTL8195A";
	else if (pDM_Odm->SupportICType == ODM_RTL8188F)
		ICType = "RTL8188F";
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
#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "RxHP", RXHP_VERSION));
#endif
	*_used = used;
	*_out_len = out_len;

}

VOID
phydm_fw_trace_en_h2c(
	IN	PVOID	pDM_VOID,
	IN	BOOLEAN		enable,
	IN	u4Byte		monitor_mode,
	IN	u4Byte		macid
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pRA_T			pRA_Table = &pDM_Odm->DM_RA_Table;
	u1Byte			H2C_Parameter[3] = {0};

	H2C_Parameter[0] = enable;
	H2C_Parameter[1] = (u1Byte)monitor_mode;
	H2C_Parameter[2] = (u1Byte)macid;
	ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("---->\n"));
	if (monitor_mode == 0){
		ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[H2C] FW_debug_en: (( %d ))\n", enable));
	} else {
		ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[H2C] FW_debug_en: (( %d )), mode: (( %d )), macid: (( %d ))\n", enable, monitor_mode, macid));
	}
	ODM_FillH2CCmd(pDM_Odm, PHYDM_H2C_FW_TRACE_EN, 3, H2C_Parameter);
}

VOID
odm_debug_trace(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		*const dm_value,
	IN		u4Byte		*_used,
	OUT		char		*output,
	IN		u4Byte		*_out_len
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u8Byte			pre_debug_components, one = 1;
	u4Byte used = *_used;
	u4Byte out_len = *_out_len;

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
		PHYDM_SNPRINTF((output + used, out_len - used, "05. (( %s ))CCK_PD\n", ((pDM_Odm->DebugComponents & ODM_COMP_CCK_PD) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "06. (( %s ))ANT_DIV\n", ((pDM_Odm->DebugComponents & ODM_COMP_ANT_DIV) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "07. (( %s ))PWR_SAVE\n", ((pDM_Odm->DebugComponents & ODM_COMP_PWR_SAVE) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "08. (( %s ))PWR_TRAIN\n", ((pDM_Odm->DebugComponents & ODM_COMP_PWR_TRAIN) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "09. (( %s ))RATE_ADAPTIVE\n", ((pDM_Odm->DebugComponents & ODM_COMP_RATE_ADAPTIVE) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "10. (( %s ))PATH_DIV\n", ((pDM_Odm->DebugComponents & ODM_COMP_PATH_DIV) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "11. (( %s ))PSD\n", ((pDM_Odm->DebugComponents & ODM_COMP_PSD) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "12. (( %s ))DYNAMIC_PRICCA\n", ((pDM_Odm->DebugComponents & ODM_COMP_DYNAMIC_PRICCA) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "13. (( %s ))RXHP\n", ((pDM_Odm->DebugComponents & ODM_COMP_RXHP) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "14. (( %s ))MP\n", ((pDM_Odm->DebugComponents & ODM_COMP_MP) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "15. (( %s ))CFO_TRACKING\n", ((pDM_Odm->DebugComponents & ODM_COMP_CFO_TRACKING) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "16. (( %s ))ACS\n", ((pDM_Odm->DebugComponents & ODM_COMP_ACS) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "17. (( %s ))ADAPTIVITY\n", ((pDM_Odm->DebugComponents & PHYDM_COMP_ADAPTIVITY) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "18. (( %s ))RA_DBG\n", ((pDM_Odm->DebugComponents & PHYDM_COMP_RA_DBG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "20. (( %s ))EDCA_TURBO\n", ((pDM_Odm->DebugComponents & ODM_COMP_EDCA_TURBO) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "21. (( %s ))EARLY_MODE\n", ((pDM_Odm->DebugComponents & ODM_COMP_EARLY_MODE) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "22. (( %s ))FW_DEBUG_TRACE\n", ((pDM_Odm->DebugComponents & ODM_FW_DEBUG_TRACE) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "24. (( %s ))TX_PWR_TRACK\n", ((pDM_Odm->DebugComponents & ODM_COMP_TX_PWR_TRACK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "25. (( %s ))RX_GAIN_TRACK\n", ((pDM_Odm->DebugComponents & ODM_COMP_RX_GAIN_TRACK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "26. (( %s ))CALIBRATION\n", ((pDM_Odm->DebugComponents & ODM_COMP_CALIBRATION) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "29. (( %s ))BEAMFORMING_DEBUG\n", ((pDM_Odm->DebugComponents & BEAMFORMING_DEBUG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "30. (( %s ))COMMON\n", ((pDM_Odm->DebugComponents & ODM_COMP_COMMON) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "31. (( %s ))INIT\n", ((pDM_Odm->DebugComponents & ODM_COMP_INIT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	} else if (dm_value[0] == 101) {
		pDM_Odm->DebugComponents = 0;
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "Disable all debug components"));
	} else {
		if (dm_value[1] == 1) { /*enable*/
			pDM_Odm->DebugComponents |= (one << dm_value[0]);

			if (dm_value[0] == 22) { /*FW trace function*/
				phydm_fw_trace_en_h2c(pDM_Odm, 1, dm_value[2], dm_value[3]); /*H2C to enable C2H Msg*/
			}
		} else if (dm_value[1] == 2) { /*disable*/
			pDM_Odm->DebugComponents &= ~(one << dm_value[0]);

			if (dm_value[0] == 22) { /*FW trace function*/
				phydm_fw_trace_en_h2c(pDM_Odm, 0, dm_value[2], dm_value[3]); /*H2C to disable C2H Msg*/
			}
		} else
			PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Warning!!!]  1:enable,  2:disable"));
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "pre-DbgComponents = 0x%x\n", (u4Byte)pre_debug_components));
	PHYDM_SNPRINTF((output + used, out_len - used, "Curr-DbgComponents = 0x%x\n", ((u4Byte)pDM_Odm->DebugComponents)));
	PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
}


struct _PHYDM_COMMAND {
	char name[16];
	u1Byte id;
};

enum PHYDM_CMD_ID {
	PHYDM_DEMO,
	PHYDM_RA,
	PHYDM_PROFILE,
	PHYDM_PATHDIV,
	PHYDM_DEBUG,
	PHYDM_SUPPORT_ABLITY
};

struct _PHYDM_COMMAND phy_dm_ary[] = {
	{"demo", PHYDM_DEMO},
	{"ra", PHYDM_RA},
	{"profile", PHYDM_PROFILE},
	{"pathdiv", PHYDM_PATHDIV},
	{"dbg", PHYDM_DEBUG},
	{"ablity", PHYDM_SUPPORT_ABLITY}
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
	int var1[5] = {0};
	int i, input_idx = 0;

	if (flag == 0) {
		PHYDM_SNPRINTF((output + used, out_len - used, "GET, nothing to print\n"));
		return;
	}

	PHYDM_SNPRINTF((output + used, out_len - used, "\n"));

	//Parsing Cmd ID
	if (input_num) {
		int n, i;

		n = sizeof(phy_dm_ary) / sizeof(struct _PHYDM_COMMAND);
		for (i = 0; i < n; i++) {
			if (strcmp(phy_dm_ary[i].name, input[0]) == 0) {
				id = phy_dm_ary[i].id;
				break;
			}
		}
		if (i == n) {
			PHYDM_SNPRINTF((output + used, out_len - used, "SET, command not found!\n"));
			return;
		}
	}

	switch (id) {
	case PHYDM_DEMO: { /*echo demo 10 0x3a z abcde >cmd*/
		u4Byte   directory;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_AP))
		char   char_temp;
#else
		u4Byte char_temp;
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

				PHYDM_SNPRINTF((output + used, out_len - used, "new SET, RA_var[%d]= (( %d ))\n", i , var1[i]));
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "odm_RA_debug\n"));*/
#if (defined(CONFIG_RA_DBG_CMD))
			odm_RA_debug(pDM_Odm, var1);
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
			odm_pathdiv_debug(pDM_Odm, var1, &used, output, &out_len);
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
			odm_debug_trace(pDM_Odm, var1, &used, output, &out_len);
		}


		break;

	case PHYDM_SUPPORT_ABLITY:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);

				/*PHYDM_SNPRINTF((output+used, out_len-used, "new SET, support ablity_var[%d]= (( %d ))\n", i , var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "support ablity\n"));*/
			phydm_support_ablity_debug(pDM_Odm, var1, &used, output, &out_len);
		}

		break;

	case PHYDM_PROFILE: /*echo profile, >cmd*/
		phydm_BasicProfile(pDM_Odm, &used, output, &out_len);
		break;

	default:
		PHYDM_SNPRINTF((output + used, out_len - used, "SET, unknown command!\n"));
		break;

	}
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
	u1Byte		temp_char_start;

	/*u1Byte	debug_trace_11byte[60];*/
	u1Byte		freg_num, c2h_seq, buf_0 = 0;

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

	strncpy(&(pDM_Odm->fw_debug_trace[pDM_Odm->c2h_cmd_start]), &CmdBuf[1], (CmdLen - 1));
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
	u1Byte	fw_debug_trace[100];
	pu1Byte	Extend_c2hDbgContent = 0;

	Extend_c2hSubID = Buffer[0];
	Extend_c2hDbgLen = Buffer[1];
	Extend_c2hDbgContent = Buffer + 2; /*DbgSeq+DbgContent  for show HEX*/

GoBackforAggreDbgPkt:
	i = 0;
	Extend_c2hDbgSeq = Buffer[2];
	Extend_c2hDbgContent = Buffer + 3;

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


