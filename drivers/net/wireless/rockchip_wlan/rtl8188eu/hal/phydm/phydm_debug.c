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
/*									ODM_COMP_ANT_DIV				|*/
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
/*									ODM_PHY_CONFIG					|*/
//									ODM_COMP_COMMON				|
//									ODM_COMP_INIT					|
//									ODM_COMP_PSD					|
/*									ODM_COMP_NOISY_DETECT			|*/
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
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rptreg of sc/bw/ht/...", value32);
	DCMD_Printf(BbDbgBuf);

	/* dbg_port = state machine */
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x007);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "state machine", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = CCA-related*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x204);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "CCA-related", (value32));
		DCMD_Printf(BbDbgBuf);
	}


	/* dbg_port = edcca/rxd*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x278);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "edcca/rxd", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = rx_state/mux_state/ADC_MASK_OFDM*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x290);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rx_state/mux_state/ADC_MASK_OFDM", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = bf-related*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x2B2);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "bf-related", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = bf-related*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0x2B8);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "bf-related", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = txon/rxd*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xA03);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "txon/rxd", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = l_rate/l_length*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xA0B);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "l_rate/l_length", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = rxd/rxd_hit*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xA0D);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rxd/rxd_hit", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = dis_cca*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAA0);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "dis_cca", (value32));
		DCMD_Printf(BbDbgBuf);
	}


	/* dbg_port = tx*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAB0);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "tx", (value32));
		DCMD_Printf(BbDbgBuf);
	}

	/* dbg_port = rx plcp*/
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD0);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rx plcp", (value32));
		DCMD_Printf(BbDbgBuf);

		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD1);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rx plcp", (value32));
		DCMD_Printf(BbDbgBuf);

		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD2);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rx plcp", (value32));
		DCMD_Printf(BbDbgBuf);

		ODM_SetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord, 0xAD3);
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_DBG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0x8fc", value32);
		DCMD_Printf(BbDbgBuf);

		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_RPT_11AC , bMaskDWord);
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "rx plcp", (value32));
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

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s\n", "BB Report Info");
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
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "F80", value32_2);
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_HT_BW", RX_HT_BW);
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_VHT_BW", RX_VHT_BW);
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_SC", RXSC);
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_HT", RX_HT);
	DCMD_Printf(BbDbgBuf);
	*/

	/*rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  RX_HT:%s ", RXHT_table[RX_HT]);*/
	/*DCMD_Printf(BbDbgBuf);*/
	RX_BW = 0;

	if (RX_HT == 2) {
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  Mode: VHT Mode");
		DCMD_Printf(BbDbgBuf);
		if (RX_VHT_BW == 0) {
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=20M");
			DCMD_Printf(BbDbgBuf);
		} else if (RX_VHT_BW == 1) {
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=40M");
			DCMD_Printf(BbDbgBuf);
		} else {
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=80M");
			DCMD_Printf(BbDbgBuf);
		}
		RX_BW = RX_VHT_BW;
	} else if (RX_HT == 1) {
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  Mode: HT Mode");
		DCMD_Printf(BbDbgBuf);
		if (RX_HT_BW == 0) {
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=20M");
			DCMD_Printf(BbDbgBuf);
		} else if (RX_HT_BW == 1) {
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=40M");
			DCMD_Printf(BbDbgBuf);
		}
		RX_BW = RX_HT_BW;
	} else {
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  Mode: Legeacy Mode");
		DCMD_Printf(BbDbgBuf);
	}

	if (RX_HT != 0) {
		if (RXSC == 0)
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  duplicate/full bw");
		else if (RXSC == 1)
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  usc20-1");
		else if (RXSC == 2)
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  lsc20-1");
		else if (RXSC == 3)
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  usc20-2");
		else if (RXSC == 4)
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  lsc20-2");
		else if (RXSC == 9)
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  usc40");
		else if (RXSC == 10)
			rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  lsc40");
		DCMD_Printf(BbDbgBuf);
	}
	/*
	if(RX_HT == 2){
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "  BW:%s", BW_table[RX_VHT_BW]);
		RX_BW = RX_VHT_BW;
		}
	else if(RX_HT == 1){
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "  BW:%s", BW_table[RX_HT_BW]);
		RX_BW = RX_HT_BW;
		}
	else
		rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE,  "");
	
	DCMD_Printf(BbDbgBuf);	
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "  RXSC:%s", RXSC_table[RXSC]);
	DCMD_Printf(BbDbgBuf);
	*/


/*	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "dB Conversion: 10log(65)", ODM_PWdB_Conversion(65,10,0));*/
/*	DCMD_Printf(BbDbgBuf);*/

	/* RX signal power and AGC related info*/

	value32 = ODM_GetBBReg(pDM_Odm, 0xF90 , bMaskDWord);
	pwDB = (u1Byte)((value32 & bMaskByte1) >> 8);
	pwDB = pwDB >> 1;
	sig_power = -110 + pwDB;
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "OFDM RX Signal Power(dB)", sig_power);
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
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "OFDM RX RF Gain(A/B/C/D)", RF_gain_pathA, RF_gain_pathA, RF_gain_pathC, RF_gain_pathD);
	DCMD_Printf(BbDbgBuf);


	/* RX Counter related info*/

	value32 = ODM_GetBBReg(pDM_Odm, 0xF08, bMaskDWord);
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "OFDM CCA Counter", ((value32&0xFFFF0000)>>16));
	DCMD_Printf(BbDbgBuf);
	
	value32 = ODM_GetBBReg(pDM_Odm, 0xFD0, bMaskDWord);
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "OFDM SBD Fail Counter", value32&0xFFFF);
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xFC4, bMaskDWord);
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "VHT SIGA/SIGB CRC8 Fail Counter", value32&0xFFFF, ((value32&0xFFFF0000)>>16));
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xFCC, bMaskDWord);
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "CCK CCA Counter", value32&0xFFFF);
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xFBC, bMaskDWord);
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "LSIG (\"Parity Fail\"/\"Rate Illegal\") Counter", value32&0xFFFF, ((value32&0xFFFF0000)>>16));
	DCMD_Printf(BbDbgBuf);

	value32_1 = ODM_GetBBReg(pDM_Odm, 0xFC8, bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xFC0, bMaskDWord);
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "HT/VHT MCS NOT SUPPORT counter", ((value32_2&0xFFFF0000)>>16), value32_1&0xFFFF);
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
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d", "RXEVM (1ss/2ss/3ss)", RXEVM_0, RXEVM_1, RXEVM_2);
	DCMD_Printf(BbDbgBuf);

/*	value32 = ODM_GetBBReg(pDM_Odm, 0xD14 ,bMaskDWord);*/
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D, dB)", RX_SNR_pathA, RX_SNR_pathB, RX_SNR_pathC, RX_SNR_pathD);
	DCMD_Printf(BbDbgBuf);
/*	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "B_RXSNR", (value32&0xFF00)>>9);*/
/*	DCMD_Printf(BbDbgBuf);*/

	value32 = ODM_GetBBReg(pDM_Odm, 0xF8C , bMaskDWord);
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "CSI_1st /CSI_2nd", value32&0xFFFF, ((value32&0xFFFF0000)>>16));
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
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "CFO Report Info");
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  Short CFO(Hz) <A/B/C/D>", SFO_A, SFO_B, SFO_C, SFO_D);
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  Long CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D);
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
	
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  Value SCFO(Hz) <A/B/C/D>", SFO_A, SFO_B, SFO_C, SFO_D);
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  ACQ CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D);
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

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  End CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D);
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

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "L-SIG");
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n   Rate:%s", L_rate[idx]);		
	DCMD_Printf(BbDbgBuf);	

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x/ %x /%x", "  Rsv/Length/Parity", rsv, RX_BW, Length);
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "HT-SIG1");
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xf2c , bMaskDWord);  /*HT SIG*/
	if (RX_HT == 1) {

		HMCSS = (u1Byte)(value32 & 0x7F);
		HRX_BW = (u1Byte)(value32 & 0x80);
		HLength = (u2Byte)((value32 >> 8) & 0xffff);
	}
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x", "  MCS/BW/Length", HMCSS, HRX_BW, HLength);
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "HT-SIG2");
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
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/ %x/ %x", "  Smooth/NoSound/Rsv/Aggregate/STBC/LDPC", smooth, htsound, rsv, agg, stbc, fec);
	DCMD_Printf(BbDbgBuf);
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x", "  SGI/E-HT-LTFs/CRC/Tail", sgi, htltf, htcrc8, Tail);
	DCMD_Printf(BbDbgBuf);


	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "VHT-SIG-A1");
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

	/*rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x", "F2C", value32);*/
	/*DCMD_Printf(BbDbgBuf);*/

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/ %x/ %x /%x /%x", "  BW/Rsv1/STBC/GID/Nsts/PAID/TXOPPS/Rsv2", vRX_BW, vrsv, vstbc, vgid, vNsts, vpaid, vtxops, vrsv2);
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "VHT-SIG-A2");
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
	
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/ %x/ %x/ %x", "  SGI/FEC/MCS/BF/Rsv/CRC/Tail", sgiext, fecext, vMCSS, bf, vrsv, vhtcrc8, vTail);
	DCMD_Printf(BbDbgBuf);

	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "VHT-SIG-B");
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
	rsprintf((char *)BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/", "  Length/Rsv/Tail/CRC", vLength, vbrsv, vbTail, vbcrc);
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
	u1Byte	legacy_table[12] = {1, 2, 5, 11, 6, 9, 12, 18, 24, 36, 48, 54};
	u1Byte	vht_en = ((pDM_Odm->RxRate) >= ODM_RATEVHTSS1MCS0) ? 1 : 0;

	if (pDM_Odm->RxRate <= ODM_RATE11M) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[CCK AGC Report] LNA_idx = 0x%x, VGA_idx = 0x%x\n",
			pDM_Odm->cck_lna_idx, pDM_Odm->cck_vga_idx));		
	} else {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[OFDM AGC Report] { 0x%x, 0x%x, 0x%x, 0x%x }\n",
			pDM_Odm->ofdm_agc_idx[0], pDM_Odm->ofdm_agc_idx[1], pDM_Odm->ofdm_agc_idx[2], pDM_Odm->ofdm_agc_idx[3]));	
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("RSSI: { %d,  %d,  %d,  %d },    RxRate: { %s%s%s%s%d%s}\n",
		(pDM_Odm->RSSI_A == 0xff) ? 0 : pDM_Odm->RSSI_A , 
		(pDM_Odm->RSSI_B == 0xff) ? 0 : pDM_Odm->RSSI_B , 
		(pDM_Odm->RSSI_C == 0xff) ? 0 : pDM_Odm->RSSI_C, 
		(pDM_Odm->RSSI_D == 0xff) ? 0 : pDM_Odm->RSSI_D,
		((pDM_Odm->RxRate >= ODM_RATEVHTSS1MCS0) && (pDM_Odm->RxRate <= ODM_RATEVHTSS1MCS9)) ? "VHT 1ss  " : "",
		((pDM_Odm->RxRate >= ODM_RATEVHTSS2MCS0) && (pDM_Odm->RxRate <= ODM_RATEVHTSS2MCS9)) ? "VHT 2ss " : "",
		((pDM_Odm->RxRate >= ODM_RATEVHTSS3MCS0) && (pDM_Odm->RxRate <= ODM_RATEVHTSS3MCS9)) ? "VHT 3ss " : "",
		(pDM_Odm->RxRate >= ODM_RATEMCS0) ? "MCS " : "",
		(vht_en) ? ((pDM_Odm->RxRate - ODM_RATEVHTSS1MCS0)%10) : ((pDM_Odm->RxRate >= ODM_RATEMCS0) ? (pDM_Odm->RxRate - ODM_RATEMCS0) : ((pDM_Odm->RxRate <= ODM_RATE54M)?legacy_table[pDM_Odm->RxRate]:0)),
		(pDM_Odm->RxRate >= ODM_RATEMCS0) ? "" : "M"));

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[CCA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",	
		FalseAlmCnt->Cnt_CCK_CCA, FalseAlmCnt->Cnt_OFDM_CCA, FalseAlmCnt->Cnt_CCA_all));

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[FA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",	
		FalseAlmCnt->Cnt_Cck_fail, FalseAlmCnt->Cnt_Ofdm_fail, FalseAlmCnt->Cnt_all));
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[OFDM FA Detail] Parity_Fail = (( %d )), Rate_Illegal = (( %d )), CRC8_fail = (( %d )), Mcs_fail = (( %d )), Fast_Fsync = (( %d )), SB_Search_fail = (( %d ))\n",	
		FalseAlmCnt->Cnt_Parity_Fail, FalseAlmCnt->Cnt_Rate_Illegal, FalseAlmCnt->Cnt_Crc8_fail, FalseAlmCnt->Cnt_Mcs_fail, FalseAlmCnt->Cnt_Fast_Fsync, FalseAlmCnt->Cnt_SB_Search_fail));
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("bLinked = %d, RSSI_Min = %d, CurrentIGI = 0x%x, bNoisy=%d\n\n",
		pDM_Odm->bLinked, pDM_Odm->RSSI_Min, pDM_DigTable->CurIGValue, pDM_Odm->NoisyDecision));    
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
#if (RTL8703B_SUPPORT == 1)
	else if (pDM_Odm->SupportICType == ODM_RTL8703B) {
		ICType = "RTL8703B";
		date = RELEASE_DATE_8703B;
		commit_by = COMMIT_BY_8703B;
		release_ver = RELEASE_VERSION_8703B;
	} 
#endif
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
#if (RTL8822B_SUPPORT == 1)  
	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PHY config 8822B", PHY_CONFIG_VERSION_8822B));
	
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

#if (RTL8822B_SUPPORT == 1)
	if ((pDM_Odm->SupportICType & ODM_RTL8822B) && (path <= ODM_RF_PATH_B)) {
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
			
			txagc = config_phydm_read_txagc_8822b(pDM_Odm, path, rate_idx);
			if (config_phydm_read_txagc_check_8822b(txagc))
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

#if (RTL8822B_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8822B) {
		if (dm_value[0] <= 1) {
			if (phydm_write_txagc_1byte_8822b(pDM_Odm, dm_value[2], dm_value[0], (u1Byte)dm_value[1]))
				PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s%x\n", "Write path-", dm_value[0], "rate index-0x", dm_value[1], " = 0x", dm_value[2]));
			else
				PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s\n", "Write path-", (dm_value[0] & 0x1), "rate index-0x", (dm_value[1] & 0x7f), " fail"));
		} else {
			PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s\n", "Write path-", (dm_value[0] & 0x1), "rate index-0x", (dm_value[1] & 0x7f), " fail"));
		}
	}
#endif
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
		PHYDM_SNPRINTF((output + used, out_len - used, "28. (( %s ))PHY_CONFIG\n", ((pDM_Odm->DebugComponents & ODM_PHY_CONFIG) ? ("V") : ("."))));
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

VOID
phydm_DumpBbReg(
	IN		PVOID			pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			Addr = 0;
	
	/* BB Reg */
	for (Addr = 0x800; Addr < 0xfff; Addr += 4)
		DbgPrint("%04x %08x\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord));

	if (pDM_Odm->SupportICType & (ODM_RTL8822B|ODM_RTL8814A)) {

		if (pDM_Odm->RFType > ODM_2T2R) {
			for (Addr = 0x1800; Addr < 0x18ff; Addr += 4)
				DbgPrint("%04x %08x\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord));
		}

		if (pDM_Odm->RFType > ODM_3T3R) {
			for (Addr = 0x1a00; Addr < 0x1aff; Addr += 4)
				DbgPrint("%04x %08x\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord));
		}

		for (Addr = 0x1900; Addr < 0x19ff; Addr += 4)
			DbgPrint("%04x %08x\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord));

		for (Addr = 0x1c00; Addr < 0x1cff; Addr += 4)
			DbgPrint("%04x %08x\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord));

		for (Addr = 0x1f00; Addr < 0x1fff; Addr += 4)
			DbgPrint("%04x %08x\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord));
	}
}

VOID
phydm_DumpAllReg(
	IN		PVOID			pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			Addr = 0;

	/* dump MAC register */
	DbgPrint("MAC==========\n");
	for (Addr = 0; Addr < 0x7ff; Addr += 4)
		DbgPrint("%04x %08x\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord));

	for (Addr = 1000; Addr < 0x17ff; Addr += 4)
		DbgPrint("%04x %08x\n", Addr, ODM_GetBBReg(pDM_Odm, Addr, bMaskDWord));

	/* dump BB register */
	DbgPrint("BB==========\n");
	phydm_DumpBbReg(pDM_Odm);

	/* dump RF register */
	DbgPrint("RF-A==========\n");
	for (Addr = 0; Addr < 0xFF; Addr++)
		DbgPrint("%02x %05x\n", Addr, ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, Addr, bRFRegOffsetMask));

	if (pDM_Odm->RFType > ODM_1T1R) {
		DbgPrint("RF-B==========\n");
		for (Addr = 0; Addr < 0xFF; Addr++)
			DbgPrint("%02x %05x\n", Addr, ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, Addr, bRFRegOffsetMask));
	}

	if (pDM_Odm->RFType > ODM_2T2R) {
		DbgPrint("RF-C==========\n");
		for (Addr = 0; Addr < 0xFF; Addr++)
			DbgPrint("%02x %05x\n", Addr, ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_C, Addr, bRFRegOffsetMask));
	}

	if (pDM_Odm->RFType > ODM_3T3R) {
		DbgPrint("RF-D==========\n");
		for (Addr = 0; Addr < 0xFF; Addr++)
			DbgPrint("%02x %05x\n", Addr, ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_D, Addr, bRFRegOffsetMask));
	}
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
	PHYDM_SUPPORT_ABILITY,
	PHYDM_GET_TXAGC,
	PHYDM_SET_TXAGC,
	PHYDM_SMART_ANT,
	PHYDM_API,
	PHYDM_TRX_PATH,
	PHYDM_LA_MODE,
	PHYDM_DUMP_REG
};

struct _PHYDM_COMMAND phy_dm_ary[] = {
	{"demo", PHYDM_DEMO},
	{"ra", PHYDM_RA},
	{"profile", PHYDM_PROFILE},
	{"pathdiv", PHYDM_PATHDIV},
	{"dbg", PHYDM_DEBUG},
	{"ability", PHYDM_SUPPORT_ABILITY},
	{"get_txagc", PHYDM_GET_TXAGC},
	{"set_txagc", PHYDM_SET_TXAGC},
	{"smtant", PHYDM_SMART_ANT},
	{"api", PHYDM_API},
	{"trxpath", PHYDM_TRX_PATH},
	{"lamode", PHYDM_LA_MODE},
	{"dumpreg", PHYDM_DUMP_REG}
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
	case PHYDM_DEMO: /*echo demo 10 0x3a z abcde >cmd*/
			{
				u4Byte   directory = 0;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_AP))				
				char   char_temp;
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

				PHYDM_SNPRINTF((output + used, out_len - used, "new SET, RA_var[%d]= (( %d ))\n", i , var1[i]));
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "odm_RA_debug\n"));*/
#if (defined(CONFIG_RA_DBG_CMD))
			odm_RA_debug((PVOID)pDM_Odm, var1);
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
			odm_debug_trace(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
		}


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
			phydm_support_ablity_debug(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
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
			phydm_hl_smart_ant_cmd(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
			#endif
			#endif
		}

		break;

	case PHYDM_API:
#if (RTL8822B_SUPPORT == 1)
	{
		if (pDM_Odm->SupportICType & ODM_RTL8822B) {
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
			config_phydm_switch_channel_bw_8822b(pDM_Odm, central_ch, primary_ch_idx, bandwidth);
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
		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);

				/*PHYDM_SNPRINTF((output+used, out_len-used, "new SET, support ablity_var[%d]= (( %d ))\n", i , var1[i]));*/
				input_idx++;
			}
		}
		
		phydm_set_txagc(pDM_Odm, (u4Byte *)var1, &used, output, &out_len);
		break;
		
	case PHYDM_TRX_PATH:
#if (RTL8822B_SUPPORT == 1)
	{
		if (pDM_Odm->SupportICType & ODM_RTL8822B) {
			u1Byte		TxPath, RxPath;
			BOOLEAN		bEnableDbgMode, bTx2Path;
			
			for (i = 0; i < 4; i++) {
				if (input[i + 1])
					PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
			}

			bEnableDbgMode = (BOOLEAN)var1[0];
			TxPath = (u1Byte) var1[1];
			RxPath = (u1Byte) var1[2];
			bTx2Path = (BOOLEAN) var1[3];

			if (bEnableDbgMode) {
				pDM_Odm->bDisablePhyApi = FALSE;
				config_phydm_trx_mode_8822b(pDM_Odm, TxPath, RxPath, bTx2Path);
				pDM_Odm->bDisablePhyApi = TRUE;
				PHYDM_SNPRINTF((output+used, out_len-used, "TxPath = 0x%x, RxPath = 0x%x, bTx2Path = %d\n", TxPath, RxPath, bTx2Path));
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

	case PHYDM_LA_MODE:
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if ((RTL8822B_SUPPORT == 1) || (RTL8814A_SUPPORT == 1))
	{
		if (pDM_Odm->SupportICType & (ODM_RTL8814A | ODM_RTL8822B)) {
			u2Byte		PollingTime;
			u1Byte		TrigSel, TrigSigSel, DmaDataSigSel, TriggerTime;
			BOOLEAN		bEnableLaMode;

			for (i = 0; i < 6; i++) {
				if (input[i + 1])
					PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
			}

			bEnableLaMode = (BOOLEAN)var1[0];
			if (bEnableLaMode) {
				TrigSel = (u1Byte)var1[1];
				TrigSigSel = (u1Byte)var1[2];
				DmaDataSigSel = (u1Byte)var1[3];
				TriggerTime = (u1Byte)var1[4];
				PollingTime = (((u1Byte)var1[5]) << 6);

				ADCSmp_Set(pDM_Odm->Adapter, TrigSel, TrigSigSel, DmaDataSigSel, TriggerTime, PollingTime);
				PHYDM_SNPRINTF((output+used, out_len-used, "TrigSel = %d, TrigSigSel = %d, DmaDataSigSel = %d\n", TrigSel, TrigSigSel, DmaDataSigSel));
				PHYDM_SNPRINTF((output+used, out_len-used, "TriggerTime = %d, PollingTime = %d\n", TriggerTime, PollingTime));
			} else {
				ADCSmp_Stop(pDM_Odm->Adapter);
				PHYDM_SNPRINTF((output+used, out_len-used, "Disable LA mode\n"));
			}
		} else
			PHYDM_SNPRINTF((output+used, out_len-used, "This IC doesn't support LA mode\n"));
	}
#else
		PHYDM_SNPRINTF((output+used, out_len-used, "This IC doesn't support LA mode\n"));
#endif
#else
		PHYDM_SNPRINTF((output+used, out_len-used, "This IC doesn't support LA mode\n"));
#endif
		break;

	case PHYDM_DUMP_REG:
	{
		u1Byte	type = 0;
		
		if (input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			type = (u1Byte)var1[0];
		}

		if (type == 0)
			phydm_DumpBbReg(pDM_Odm);
		else if (type == 1)
			phydm_DumpAllReg(pDM_Odm);
	}
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
	if(function == RATE_DECISION) {
		if(dbg_num == 0) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin] RA_CNT=((%d))  Max_device=((%d))--------------------------->\n", content_1, content_2));
			} else if(content_0 == 2) {
				 ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin] Check RA macid= ((%d)), MediaStatus=((%d)), Dis_RA=((%d)),  try_bit=((0x%x))\n", content_1, content_2, content_3, content_4));
			} else if(content_0 == 3) {
				 ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateDecisoin] Check RA  total=((%d)),  drop=((0x%x)), TXRPT_TRY_bit=((%x)), bNoisy=((%x))\n", content_1, content_2, content_3, content_4));
			}
		} else if(dbg_num == 1) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin] RTY[0,1,2,3]=[ %d,  %d,  %d,  %d ] \n", content_1, content_2, content_3, content_4));
			} else if(content_0 == 2) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin] RTY[4]=[ %d ], drop=((%d)), total=((%d)),  current_rate=((0x%x))\n", content_1, content_2, content_3, content_4));
			} else if(content_0 == 3) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin] penality_idx=((%d ))\n", content_1));
			}
		}
		
		else if(dbg_num == 3) {
			if (content_0 == 1)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateDecisoin] Fast_RA (( DOWN ))  total=((%d)),  total>>1=((%d)), R4+R3+R2 = ((%d)), RateDownHold = ((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 2)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateDecisoin] Fast_RA (( UP ))  total_acc=((%d)),  total_acc>>1=((%d)), R4+R3+R2 = ((%d)), RateDownHold = ((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 3)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateDecisoin] Fast_RA (( UP )) ((Rate Down Hold))  RA_CNT=((%d))\n", content_1));
			else if (content_0 == 4)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateDecisoin] Fast_RA (( UP )) ((tota_accl<5 skip))  RA_CNT=((%d))\n", content_1));
			else if (content_0 == 8)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateDecisoin] Fast_RA (( Reset Tx Rpt )) RA_CNT=((%d))\n", content_1));
		}
		
		else if(dbg_num == 5) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin]  (( UP))  Nsc=((%d)), N_High=((%d))\n", content_1, content_2));
			} else if(content_0 == 2) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin]  ((DOWN))  Nsc=((%d)), N_Low=((%d))\n", content_1, content_2));
			} else if(content_0 == 3) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin]  ((HOLD))  Nsc=((%d)), N_High=((%d)), N_Low=((%d)), Reset_CNT=((%d))\n", content_1, content_2, content_3, content_4));
			}
		}
		else if(dbg_num == 0x60) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin]  ((AP RPT))  macid=((%d)), BUPDATE[macid]=((%d))\n", content_1, content_2));
			} else if(content_0 == 4) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin]  ((AP RPT))  pass=((%d)), rty_num=((%d)), drop=((%d)), total=((%d))\n", content_1, content_2, content_3, content_4));
			} else if(content_0 == 5) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW][RateDecisoin]  ((AP RPT))  PASS=((%d)), RTY_NUM=((%d)), DROP=((%d)), TOTAL=((%d))\n", content_1, content_2, content_3, content_4));
			}
		}
		else if(dbg_num == 0xff) {
			if(content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("\n\n"));
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
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateDownStep]  ((Rate Down)), macid=((%d)),  rate=((0x%x)),  BW=((%d))\n", content_1, content_2, content_3));
			}
		}
	} else if (function == TRY_DONE) {
		if (dbg_num == 1) {
			if (content_0 == 1) {
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][Try Done]  ((try succsess )) macid=((%d)), Try_Done_cnt=((%d))\n", content_1, content_2));
				/**/
			}
		} else if (dbg_num == 2) {
			if (content_0 == 1)
				ODM_RT_TRACE(pDM_Odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][Try Done]  ((try fail )) macid=((%d)), Try_Done_cnt=((%d)),  multi_try_rate=((%d))\n", content_1, content_2, content_3));
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
	}
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


