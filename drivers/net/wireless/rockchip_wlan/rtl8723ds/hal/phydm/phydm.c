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

const u2Byte dB_Invert_Table[12][8] = {
	{	1,		1,		1,		2,		2,		2,		2,		3},
	{	3,		3,		4,		4,		4,		5,		6,		6},
	{	7,		8,		9,		10,		11,		13,		14,		16},
	{	18,		20,		22,		25,		28,		32,		35,		40},
	{	45,		50,		56,		63,		71,		79,		89,		100},
	{	112,		126,		141,		158,		178,		200,		224,		251},
	{	282,		316,		355,		398,		447,		501,		562,		631},
	{	708,		794,		891,		1000,	1122,	1259,	1413,	1585},
	{	1778,	1995,	2239,	2512,	2818,	3162,	3548,	3981},
	{	4467,	5012,	5623,	6310,	7079,	7943,	8913,	10000},
	{	11220,	12589,	14125,	15849,	17783,	19953,	22387,	25119},
	{	28184,	31623,	35481,	39811,	44668,	50119,	56234,	65535}
};


//============================================================
// Local Function predefine.
//============================================================

/* START------------COMMON INFO RELATED--------------- */

VOID
odm_GlobalAdapterCheck(
	IN		VOID
	);

//move to odm_PowerTacking.h by YuChen



VOID
odm_UpdatePowerTrainingState(
	IN	PDM_ODM_T	pDM_Odm
);

//============================================================
//3 Export Interface
//============================================================

/*Y = 10*log(X)*/
s4Byte
ODM_PWdB_Conversion(
	IN  s4Byte X,
	IN  u4Byte TotalBit,
	IN  u4Byte DecimalBit
	)
{
	s4Byte Y, integer = 0, decimal = 0;
	u4Byte i;

	if(X == 0)
		X = 1; // log2(x), x can't be 0

	for(i = (TotalBit-1); i > 0; i--)
	{
		if(X & BIT(i))
		{
			integer = i;
			if(i > 0)
				decimal = (X & BIT(i-1))?2:0; //decimal is 0.5dB*3=1.5dB~=2dB 
			break;
		}
	}
	
	Y = 3*(integer-DecimalBit)+decimal; //10*log(x)=3*log2(x), 

	return Y;
}

s4Byte
ODM_SignConversion(
    IN  s4Byte value,
    IN  u4Byte TotalBit
    )
{
	if(value&BIT(TotalBit-1))
		value -= BIT(TotalBit);
	return value;
}

void
phydm_seq_sorting( 
	IN		PVOID	pDM_VOID,
	IN OUT	u4Byte	*p_value,
	IN OUT	u4Byte	*rank_idx,
	IN OUT	u4Byte	*p_idx_out,	
	IN		u1Byte	seq_length
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte		i = 0 , j = 0;
	u4Byte		tmp_a, tmp_b;
	u4Byte		tmp_idx_a, tmp_idx_b;

	for (i = 0; i < seq_length; i++) {
		rank_idx[i] = i;
		/**/
	}

	for (i = 0; i < (seq_length - 1); i++) {
		
		for (j = 0; j < (seq_length - 1 - i); j++) {
		
			tmp_a = p_value[j];
			tmp_b = p_value[j+1];

			tmp_idx_a = rank_idx[j];
			tmp_idx_b = rank_idx[j+1];

			if (tmp_a < tmp_b) {
				p_value[j] = tmp_b;
				p_value[j+1] = tmp_a;
				
				rank_idx[j] = tmp_idx_b;
				rank_idx[j+1] = tmp_idx_a;
			}
		}		
	}

	for (i = 0; i < seq_length; i++) {
		p_idx_out[rank_idx[i]] = i+1;
		/**/
	}
	

	
}

VOID
ODM_InitMpDriverStatus(
	IN		PDM_ODM_T		pDM_Odm
)
{
#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)

	// Decide when compile time
	#if(MP_DRIVER == 1)
	pDM_Odm->mp_mode = TRUE;
	#else
	pDM_Odm->mp_mode = FALSE;
	#endif

#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)

	PADAPTER	Adapter =  pDM_Odm->Adapter;

	// Update information every period
	pDM_Odm->mp_mode = (BOOLEAN)Adapter->registrypriv.mp_mode;

#else

	prtl8192cd_priv	 priv = pDM_Odm->priv;

	pDM_Odm->mp_mode = (BOOLEAN)priv->pshare->rf_ft_var.mp_specific;
	
#endif
}

VOID
ODM_UpdateMpDriverStatus(
	IN		PDM_ODM_T		pDM_Odm
)
{
#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)

	// Do nothing.

#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	PADAPTER	Adapter =  pDM_Odm->Adapter;

	// Update information erery period
	pDM_Odm->mp_mode = (BOOLEAN)Adapter->registrypriv.mp_mode;

#else

	// Do nothing.

#endif
}

VOID
PHYDM_InitTRXAntennaSetting(
	IN		PDM_ODM_T		pDM_Odm
)
{
/*#if (RTL8814A_SUPPORT == 1)*/

	if (pDM_Odm->SupportICType & (ODM_RTL8814A)) {
		u1Byte	RxAnt = 0, TxAnt = 0;

		RxAnt = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG(BB_RX_PATH, pDM_Odm), ODM_BIT(BB_RX_PATH, pDM_Odm));
		TxAnt = (u1Byte)ODM_GetBBReg(pDM_Odm, ODM_REG(BB_TX_PATH, pDM_Odm), ODM_BIT(BB_TX_PATH, pDM_Odm));
		pDM_Odm->TXAntStatus =  (TxAnt & 0xf);
		pDM_Odm->RXAntStatus =  (RxAnt & 0xf);
	} else if (pDM_Odm->SupportICType & (ODM_RTL8723D | ODM_RTL8821C)) {
		pDM_Odm->TXAntStatus = 0x1;
		pDM_Odm->RXAntStatus = 0x1;
			
	}
/*#endif*/
}

void
phydm_traffic_load_decision( 
	IN		PVOID	pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	
	/*---TP & Trafic-load calculation---*/

	if (pDM_Odm->lastTxOkCnt > (*(pDM_Odm->pNumTxBytesUnicast)))
		pDM_Odm->lastTxOkCnt = (*(pDM_Odm->pNumTxBytesUnicast));

	if (pDM_Odm->lastRxOkCnt > (*(pDM_Odm->pNumRxBytesUnicast)))
		pDM_Odm->lastRxOkCnt = (*(pDM_Odm->pNumRxBytesUnicast));
	
	pDM_Odm->curTxOkCnt =  *(pDM_Odm->pNumTxBytesUnicast) - pDM_Odm->lastTxOkCnt;
	pDM_Odm->curRxOkCnt =  *(pDM_Odm->pNumRxBytesUnicast) - pDM_Odm->lastRxOkCnt;
	pDM_Odm->lastTxOkCnt =  *(pDM_Odm->pNumTxBytesUnicast);
	pDM_Odm->lastRxOkCnt =  *(pDM_Odm->pNumRxBytesUnicast);

	#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	pDM_Odm->tx_tp = ((pDM_Odm->tx_tp)>>1) + (u4Byte)(((pDM_Odm->curTxOkCnt)>>17)>>1); /* <<3(8bit), >>20(10^6,M)*/
	pDM_Odm->rx_tp = ((pDM_Odm->rx_tp)>>1) + (u4Byte)(((pDM_Odm->curRxOkCnt)>>17)>>1); /* <<3(8bit), >>20(10^6,M)*/
	#else
	pDM_Odm->tx_tp = ((pDM_Odm->tx_tp)>>1) + (u4Byte)(((pDM_Odm->curTxOkCnt)>>18)>>1); /* <<3(8bit), >>20(10^6,M), >>1(2sec)*/
	pDM_Odm->rx_tp = ((pDM_Odm->rx_tp)>>1) + (u4Byte)(((pDM_Odm->curRxOkCnt)>>18)>>1); /* <<3(8bit), >>20(10^6,M), >>1(2sec)*/
	#endif
	pDM_Odm->total_tp = pDM_Odm->tx_tp + pDM_Odm->rx_tp;

	/*
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("curTxOkCnt = %d, curRxOkCnt = %d, lastTxOkCnt = %d, lastRxOkCnt = %d\n",
		pDM_Odm->curTxOkCnt, pDM_Odm->curRxOkCnt, pDM_Odm->lastTxOkCnt, pDM_Odm->lastRxOkCnt));
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("tx_tp = %d, rx_tp = %d\n",
		pDM_Odm->tx_tp, pDM_Odm->rx_tp));
	*/

	pDM_Odm->pre_TrafficLoad = pDM_Odm->TrafficLoad;
	
	if (pDM_Odm->curTxOkCnt > 1875000 || pDM_Odm->curRxOkCnt > 1875000) {		/* ( 1.875M * 8bit ) / 2sec= 7.5M bits /sec )*/
	
		pDM_Odm->TrafficLoad = TRAFFIC_HIGH;
		/**/
	} else if (pDM_Odm->curTxOkCnt > 500000 || pDM_Odm->curRxOkCnt > 500000) { /*( 0.5M * 8bit ) / 2sec =  2M bits /sec )*/
	
		pDM_Odm->TrafficLoad = TRAFFIC_MID;
		/**/
	} else if (pDM_Odm->curTxOkCnt > 100000 || pDM_Odm->curRxOkCnt > 100000)  { /*( 0.1M * 8bit ) / 2sec =  0.4M bits /sec )*/
	
		pDM_Odm->TrafficLoad = TRAFFIC_LOW;
		/**/
	} else {
	
		pDM_Odm->TrafficLoad = TRAFFIC_ULTRA_LOW;
		/**/
	}
}

VOID
phydm_config_ofdm_tx_path(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u4Byte			path
)
{
	u1Byte	ofdm_rx_path;

	#if (RTL8192E_SUPPORT == 1)
	if (pDM_Odm->SupportICType & (ODM_RTL8192E)) {
		
		if (path == PHYDM_A) {
			ODM_SetBBReg(pDM_Odm, 0x90c , bMaskDWord, 0x81121111);
			/**/
		} else if (path == PHYDM_B) {
			ODM_SetBBReg(pDM_Odm, 0x90c , bMaskDWord, 0x82221222);
			/**/
		} else  if (path == PHYDM_AB) {
			ODM_SetBBReg(pDM_Odm, 0x90c , bMaskDWord, 0x83321333);
			/**/
		}
		
		
	}
	#endif
}

VOID
phydm_config_ofdm_rx_path(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u4Byte			path
)
{
	u1Byte	ofdm_rx_path = 0;

	#if (RTL8192E_SUPPORT == 1)
	if (pDM_Odm->SupportICType & (ODM_RTL8192E)) {
		
		if (path == PHYDM_A) {
			ofdm_rx_path = 1;
			/**/
		} else if (path == PHYDM_B) {
			ofdm_rx_path = 2;
			/**/
		} else  if (path == PHYDM_AB) {
			ofdm_rx_path = 3;
			/**/
		}
		
		ODM_SetBBReg(pDM_Odm, 0xC04 , 0xff, (((ofdm_rx_path)<<4)|ofdm_rx_path));
		ODM_SetBBReg(pDM_Odm, 0xD04 , 0xf, ofdm_rx_path);
	}
	#endif
}

VOID
phydm_config_cck_rx_antenna_init(
	IN		PDM_ODM_T		pDM_Odm
)
{
	#if (RTL8192E_SUPPORT == 1)
	if (pDM_Odm->SupportICType & (ODM_RTL8192E)) {
	
		/*CCK 2R CCA parameters*/
		ODM_SetBBReg(pDM_Odm, 0xa2c , BIT18, 1); /*enable 2R Rx path*/
		ODM_SetBBReg(pDM_Odm, 0xa2c , BIT22, 1); /*enable 2R MRC*/
		ODM_SetBBReg(pDM_Odm, 0xa84 , BIT28, 1); /*1. pdx1[5:0] > 2*PD_lim 2. RXIQ_3 = 0 ( signed )*/
		ODM_SetBBReg(pDM_Odm, 0xa70 , BIT7, 0); /*Concurrent CCA at LSB & USB*/
		ODM_SetBBReg(pDM_Odm, 0xa74 , BIT8, 0); /*RX path diversity enable*/
		ODM_SetBBReg(pDM_Odm, 0xa08 , BIT28, 1); /* r_cck_2nd_sel_eco*/
		ODM_SetBBReg(pDM_Odm, 0xa14 , BIT7, 0); /* r_en_mrc_antsel*/
	}
	#endif
}

VOID
phydm_config_cck_rx_path(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			path,
	IN		u1Byte			path_div_en
)
{
	u1Byte	path_div_select = 0;
	u1Byte	cck_1_path = 0, cck_2_path = 0;

	#if (RTL8192E_SUPPORT == 1)
	if (pDM_Odm->SupportICType & (ODM_RTL8192E)) {
		
		if (path == PHYDM_A) {
			path_div_select = 0;
			cck_1_path = 0;
			cck_2_path = 0;
		} else if (path == PHYDM_B) {
			path_div_select = 0;
			cck_1_path = 1;
			cck_2_path = 1;			
		} else  if (path == PHYDM_AB) {
		
			if (path_div_en == CCA_PATHDIV_ENABLE)
				path_div_select = 1;
			
			cck_1_path = 0;
			cck_2_path = 1;	
			
		}
		
		ODM_SetBBReg(pDM_Odm, 0xa04 , (BIT27|BIT26), cck_1_path);
		ODM_SetBBReg(pDM_Odm, 0xa04 , (BIT25|BIT24), cck_2_path);
		ODM_SetBBReg(pDM_Odm, 0xa74 , BIT8, path_div_select);
		
	}
	#endif
}

VOID
phydm_config_trx_path(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		*const dm_value,
	IN		u4Byte		*_used,
	OUT		char			*output,
	IN		u4Byte		*_out_len
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			pre_support_ability;
	u4Byte used = *_used;
	u4Byte out_len = *_out_len;

	/* CCK */
	if (dm_value[0] == 0) {
		
		if (dm_value[1] == 1) { /*TX*/
			if (dm_value[2] == 1)
				ODM_SetBBReg(pDM_Odm, 0xa04, 0xf0000000, 0x8);
			else if (dm_value[2] == 2)
				ODM_SetBBReg(pDM_Odm, 0xa04, 0xf0000000, 0x4);
			else if (dm_value[2] == 3)
				ODM_SetBBReg(pDM_Odm, 0xa04, 0xf0000000, 0xc);
		} else if (dm_value[1] == 2) { /*RX*/
		
			phydm_config_cck_rx_antenna_init(pDM_Odm);
			
			if (dm_value[2] == 1) {
				phydm_config_cck_rx_path(pDM_Odm, PHYDM_A, CCA_PATHDIV_DISABLE);
			} else  if (dm_value[2] == 2) {
				phydm_config_cck_rx_path(pDM_Odm, PHYDM_B, CCA_PATHDIV_DISABLE);
			} else  if (dm_value[2] == 3) {
				if (dm_value[3] == 1) /*enable path diversity*/
					phydm_config_cck_rx_path(pDM_Odm, PHYDM_AB, CCA_PATHDIV_ENABLE);
				else
					phydm_config_cck_rx_path(pDM_Odm, PHYDM_B, CCA_PATHDIV_DISABLE);
			}
		}
	} 
	/* OFDM */
	else if (dm_value[0] == 1) {

		if (dm_value[1] == 1) { /*TX*/
			phydm_config_ofdm_tx_path(pDM_Odm, dm_value[2]);
			/**/
		} else if (dm_value[1] == 2) { /*RX*/
			phydm_config_ofdm_rx_path(pDM_Odm, dm_value[2]);
			/**/
		}
	}

	PHYDM_SNPRINTF((output+used, out_len-used, "PHYDM Set Path [%s] [%s] = [%s%s%s%s]\n", 
		(dm_value[0] == 1) ? "OFDM" : "CCK",
		(dm_value[1] == 1) ? "TX" : "RX",
		(dm_value[2] & 0x1)?"A":"",
		(dm_value[2] & 0x2)?"B":"",
		(dm_value[2] & 0x4)?"C":"",
		(dm_value[2] & 0x8)?"D":""
		));
		
}

VOID
phydm_Init_cck_setting(
	IN		PDM_ODM_T		pDM_Odm
)
{
	u4Byte value_824,value_82c;

	pDM_Odm->bCckHighPower = (BOOLEAN) ODM_GetBBReg(pDM_Odm, ODM_REG(CCK_RPT_FORMAT,pDM_Odm), ODM_BIT(CCK_RPT_FORMAT,pDM_Odm));

	#if (RTL8192E_SUPPORT == 1)
	if(pDM_Odm->SupportICType & (ODM_RTL8192E))
	{
		#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		phydm_config_cck_rx_antenna_init(pDM_Odm);
		phydm_config_cck_rx_path(pDM_Odm, PHYDM_A, CCA_PATHDIV_DISABLE);
		#endif
	
		/* 0x824[9] = 0x82C[9] = 0xA80[7]  those registers setting should be equal or CCK RSSI report may be incorrect */
		value_824 = ODM_GetBBReg(pDM_Odm, 0x824, BIT9);
		value_82c = ODM_GetBBReg(pDM_Odm, 0x82c, BIT9);
		
		if(value_824 != value_82c)
		{
			ODM_SetBBReg(pDM_Odm, 0x82c , BIT9, value_824);
		}
		ODM_SetBBReg(pDM_Odm, 0xa80 , BIT7, value_824);
		pDM_Odm->cck_agc_report_type = (BOOLEAN)value_824;

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("cck_agc_report_type = (( %d )), ExtLNAGain = (( %d ))\n", pDM_Odm->cck_agc_report_type, pDM_Odm->ExtLNAGain));
	}
	#endif
	
#if ((RTL8703B_SUPPORT == 1) || (RTL8723D_SUPPORT == 1))
	if (pDM_Odm->SupportICType & (ODM_RTL8703B|ODM_RTL8723D)) {

		pDM_Odm->cck_agc_report_type = ODM_GetBBReg(pDM_Odm, 0x950, BIT11) ? 1 : 0; /*1: 4bit LNA , 0: 3bit LNA */
		
		if (pDM_Odm->cck_agc_report_type != 1) {
			DbgPrint("[Warning] 8703B/8723D CCK should be 4bit LNA, ie. 0x950[11] = 1\n");
			/**/
		}
	}
#endif
	
#if ((RTL8723D_SUPPORT == 1) || (RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1))

	if (pDM_Odm->SupportICType & (ODM_RTL8723D|ODM_RTL8822B|ODM_RTL8197F)) {
		pDM_Odm->cck_new_agc = ODM_GetBBReg(pDM_Odm, 0xa9c, BIT17)?TRUE:FALSE;              /*1: new agc  0: old agc*/
	} else
#endif
		pDM_Odm->cck_new_agc = FALSE;
	
}

VOID
PHYDM_InitSoftMLSetting(
	IN		PDM_ODM_T		pDM_Odm
)
{
#if (RTL8822B_SUPPORT == 1)
	if (pDM_Odm->mp_mode == FALSE) {
		if (pDM_Odm->SupportICType & ODM_RTL8822B)
			ODM_SetBBReg(pDM_Odm, 0x19a8, bMaskDWord, 0xc10a0000);
	}
#endif
}

VOID
PHYDM_InitHwInfoByRfe(
	IN		PDM_ODM_T		pDM_Odm
)
{
#if (RTL8822B_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		phydm_init_hw_info_by_rfe_type_8822b(pDM_Odm);
#endif
}

VOID
odm_CommonInfoSelfInit(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	phydm_Init_cck_setting(pDM_Odm);
	pDM_Odm->RFPathRxEnable = (u1Byte) ODM_GetBBReg(pDM_Odm, ODM_REG(BB_RX_PATH,pDM_Odm), ODM_BIT(BB_RX_PATH,pDM_Odm));
#if (DM_ODM_SUPPORT_TYPE != ODM_CE)	
	pDM_Odm->pbNet_closed = &pDM_Odm->BOOLEAN_temp;

	PHYDM_InitDebugSetting(pDM_Odm);
#endif
	ODM_InitMpDriverStatus(pDM_Odm);
	PHYDM_InitTRXAntennaSetting(pDM_Odm);
	PHYDM_InitSoftMLSetting(pDM_Odm);

	pDM_Odm->phydm_period = PHYDM_WATCH_DOG_PERIOD;
	pDM_Odm->phydm_sys_up_time = 0;

	if (pDM_Odm->SupportICType & ODM_IC_1SS) 
		pDM_Odm->num_rf_path = 1;
	else if (pDM_Odm->SupportICType & ODM_IC_2SS)
		pDM_Odm->num_rf_path = 2;
	else if(pDM_Odm->SupportICType & ODM_IC_3SS )
		pDM_Odm->num_rf_path = 3;
	else if(pDM_Odm->SupportICType & ODM_IC_4SS )
		pDM_Odm->num_rf_path = 4;

	pDM_Odm->TxRate = 0xFF;

	pDM_Odm->number_linked_client = 0;
	pDM_Odm->pre_number_linked_client = 0;
	pDM_Odm->number_active_client = 0;
	pDM_Odm->pre_number_active_client = 0;

	pDM_Odm->lastTxOkCnt = 0;
	pDM_Odm->lastRxOkCnt = 0;
	pDM_Odm->tx_tp = 0;
	pDM_Odm->rx_tp = 0;
	pDM_Odm->total_tp = 0;
	pDM_Odm->TrafficLoad = TRAFFIC_LOW;

	pDM_Odm->nbi_set_result = 0;
	
}

VOID
odm_CommonInfoSelfUpdate(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	u1Byte	EntryCnt = 0, num_active_client = 0;
	u4Byte	i, OneEntry_MACID = 0, ma_rx_tp = 0;
	PSTA_INFO_T   	pEntry;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

	PADAPTER	Adapter =  pDM_Odm->Adapter;
	PMGNT_INFO	pMgntInfo = &Adapter->MgntInfo;

	pEntry = pDM_Odm->pODM_StaInfo[0];
	if (pMgntInfo->mAssoc) {
		pEntry->bUsed = TRUE;
		for (i = 0; i < 6; i++)
			pEntry->MacAddr[i] = pMgntInfo->Bssid[i];
	} else if (GetFirstClientPort(Adapter)) {
		PADAPTER	pClientAdapter = GetFirstClientPort(Adapter);

		pEntry->bUsed = TRUE;
		for (i = 0; i < 6; i++)
			pEntry->MacAddr[i] = pClientAdapter->MgntInfo.Bssid[i];
	} else {
		pEntry->bUsed = FALSE;
		for (i = 0; i < 6; i++)
			pEntry->MacAddr[i] = 0;
	}

	//STA mode is linked to AP
	if(IS_STA_VALID(pDM_Odm->pODM_StaInfo[0]) && !ACTING_AS_AP(Adapter))
		pDM_Odm->bsta_state = TRUE;
	else
		pDM_Odm->bsta_state = FALSE;
#endif

/* THis variable cannot be used because it is wrong*/
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (*(pDM_Odm->pBandWidth) == ODM_BW40M)
	{
		if (*(pDM_Odm->pSecChOffset) == 1)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel) + 2;
		else if (*(pDM_Odm->pSecChOffset) == 2)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel) - 2;
	} else if (*(pDM_Odm->pBandWidth) == ODM_BW80M)	{
		if (*(pDM_Odm->pSecChOffset) == 1)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel) + 6;
		else if (*(pDM_Odm->pSecChOffset) == 2)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel) - 6;
	} else
		pDM_Odm->ControlChannel = *(pDM_Odm->pChannel);
#else
	if (*(pDM_Odm->pBandWidth) == ODM_BW40M) {
		if (*(pDM_Odm->pSecChOffset) == 1)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel) - 2;
		else if (*(pDM_Odm->pSecChOffset) == 2)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel) + 2;
	} else
		pDM_Odm->ControlChannel = *(pDM_Odm->pChannel);
#endif

	for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		pEntry = pDM_Odm->pODM_StaInfo[i];
		if(IS_STA_VALID(pEntry))
		{
			EntryCnt++;
			if(EntryCnt==1)
			{
				OneEntry_MACID=i;
			}

			#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
				ma_rx_tp =  (pEntry->rx_byte_cnt_LowMAW)<<3; /*  low moving average RX  TP   ( bit /sec)*/

				ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("ClientTP[%d]: ((%d )) bit/sec\n", i, ma_rx_tp));
				
				if (ma_rx_tp > ACTIVE_TP_THRESHOLD)
					num_active_client++;
			#endif
                }
	}
	
	if(EntryCnt == 1)
	{
		pDM_Odm->bOneEntryOnly = TRUE;
		pDM_Odm->OneEntry_MACID=OneEntry_MACID;
	}
	else
		pDM_Odm->bOneEntryOnly = FALSE;

	pDM_Odm->pre_number_linked_client = pDM_Odm->number_linked_client;
	pDM_Odm->pre_number_active_client = pDM_Odm->number_active_client;
	
	pDM_Odm->number_linked_client = EntryCnt;
	pDM_Odm->number_active_client = num_active_client;	

	/* Update MP driver status*/
	ODM_UpdateMpDriverStatus(pDM_Odm);

	/*Traffic load information update*/
	phydm_traffic_load_decision(pDM_Odm);	

	pDM_Odm->phydm_sys_up_time += pDM_Odm->phydm_period;
}

VOID
odm_CommonInfoSelfReset(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	pDM_Odm->PhyDbgInfo.NumQryBeaconPkt = 0;
#endif
}

PVOID
PhyDM_Get_Structure(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			Structure_Type
)

{
	PVOID	pStruct = NULL;
#if RTL8195A_SUPPORT
	switch (Structure_Type){
		case	PHYDM_FALSEALMCNT:
			pStruct = &FalseAlmCnt;
		break;
		
		case	PHYDM_CFOTRACK:
			pStruct = &DM_CfoTrack;
		break;

		case	PHYDM_ADAPTIVITY:
			pStruct = &(pDM_Odm->Adaptivity);
		break;
		
		default:
		break;
	}

#else
	switch (Structure_Type){
		case	PHYDM_FALSEALMCNT:
			pStruct = &(pDM_Odm->FalseAlmCnt);
		break;
		
		case	PHYDM_CFOTRACK:
			pStruct = &(pDM_Odm->DM_CfoTrack);
		break;

		case	PHYDM_ADAPTIVITY:
			pStruct = &(pDM_Odm->Adaptivity);
		break;
		
		default:
		break;
	}

#endif
	return	pStruct;
}

VOID
odm_HWSetting(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if (RTL8821A_SUPPORT == 1)
	if(pDM_Odm->SupportICType & ODM_RTL8821)
		odm_HWSetting_8821A(pDM_Odm);
#endif

#if (RTL8814A_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		phydm_hwsetting_8814a(pDM_Odm);
#endif
#if (RTL8822B_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8822B)
		phydm_hwsetting_8822b(pDM_Odm);
#endif
}
#if SUPPORTABLITY_PHYDMLIZE
VOID
phydm_supportability_Init(
	IN		PVOID		pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			support_ability = 0;
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	#endif

	if (pDM_Odm->SupportICType != ODM_RTL8821C)
		return;

	switch (pDM_Odm->SupportICType)
	{

	/*---------------N Series--------------------*/
		case	ODM_RTL8188E:
			support_ability |= 
				ODM_BB_DIG 			|
				ODM_BB_RA_MASK		|
				ODM_BB_DYNAMIC_TXPWR	|
				ODM_BB_FA_CNT			|
				ODM_BB_RSSI_MONITOR	|
				ODM_BB_CCK_PD			|
				ODM_RF_TX_PWR_TRACK	|
				ODM_RF_RX_GAIN_TRACK	|
				ODM_RF_CALIBRATION		|
				ODM_MAC_EDCA_TURBO	|
				ODM_MAC_EARLY_MODE	|
				ODM_BB_CFO_TRACKING	|
				ODM_BB_NHM_CNT		|
				ODM_BB_PRIMARY_CCA;
			break;
			
		case	ODM_RTL8192E:
			support_ability |= 	
				ODM_BB_DIG 			|
				ODM_RF_TX_PWR_TRACK	|
				ODM_BB_RA_MASK		|
				ODM_BB_FA_CNT			|
				ODM_BB_RSSI_MONITOR	|
				ODM_MAC_EDCA_TURBO	|
				ODM_BB_CFO_TRACKING	|
/*				ODM_BB_PWR_TRAIN		|*/
				ODM_BB_NHM_CNT		|
				ODM_BB_PRIMARY_CCA;
			break;
			
		case	ODM_RTL8723B:
			support_ability |= 	
				ODM_BB_DIG 			|
				ODM_BB_RA_MASK		|
				ODM_BB_FA_CNT			|
				ODM_BB_RSSI_MONITOR	|
				ODM_BB_CCK_PD			|
				ODM_RF_TX_PWR_TRACK	|
				ODM_RF_RX_GAIN_TRACK	|
				ODM_RF_CALIBRATION		|
				ODM_MAC_EDCA_TURBO	|
				ODM_BB_CFO_TRACKING	|
/*				ODM_BB_PWR_TRAIN		|*/
				ODM_BB_NHM_CNT;
			break;
			
		case	ODM_RTL8703B:
			support_ability |= 	
				ODM_BB_DIG 			|
				ODM_BB_RA_MASK		|
				ODM_BB_FA_CNT			|
				ODM_BB_RSSI_MONITOR	|
				ODM_BB_CCK_PD			|
				ODM_BB_CFO_TRACKING	|
				//ODM_BB_PWR_TRAIN	|
				ODM_BB_NHM_CNT		|
				ODM_RF_TX_PWR_TRACK	|
				//ODM_RF_RX_GAIN_TRACK	|
				ODM_RF_CALIBRATION		|
				ODM_MAC_EDCA_TURBO;
			break;
			
		case	ODM_RTL8723D:	
			support_ability |=	
				ODM_BB_DIG				|
				ODM_BB_RA_MASK		|
				ODM_BB_FA_CNT			|
				ODM_BB_RSSI_MONITOR 	|
				ODM_BB_CCK_PD			|
				ODM_BB_CFO_TRACKING 	|
				//ODM_BB_PWR_TRAIN	|
				ODM_BB_NHM_CNT		|
				ODM_RF_TX_PWR_TRACK	|
				//ODM_RF_RX_GAIN_TRACK	|
				//ODM_RF_CALIBRATION	|
				ODM_MAC_EDCA_TURBO;
			break;
			
		case	ODM_RTL8188F:
			support_ability |= 	
				ODM_BB_DIG 			|
				ODM_BB_RA_MASK		|
				ODM_BB_FA_CNT			|
				ODM_BB_RSSI_MONITOR	|
				ODM_BB_CCK_PD			|
				ODM_MAC_EDCA_TURBO	|
				ODM_BB_CFO_TRACKING	|
				ODM_BB_NHM_CNT		|
				ODM_RF_TX_PWR_TRACK	|	
				ODM_RF_CALIBRATION;
			break;
	/*---------------AC Series-------------------*/
	
		case	ODM_RTL8812:
		case	ODM_RTL8821:
			support_ability |=
				ODM_BB_DIG 			|
				ODM_BB_FA_CNT			|
				ODM_BB_RSSI_MONITOR	|
				ODM_BB_RA_MASK		|
				ODM_RF_TX_PWR_TRACK	|
				ODM_MAC_EDCA_TURBO	|
				ODM_BB_CFO_TRACKING	|
/*				ODM_BB_PWR_TRAIN		|*/
				ODM_BB_DYNAMIC_TXPWR	|
				ODM_BB_NHM_CNT;
			break;
			
		case ODM_RTL8814A:
			support_ability |=
				ODM_BB_DIG				|
				ODM_BB_FA_CNT			|
				ODM_BB_RSSI_MONITOR	| 
				ODM_BB_RA_MASK		|
				ODM_RF_TX_PWR_TRACK	|
				ODM_BB_CCK_PD			|
				ODM_MAC_EDCA_TURBO	|
				ODM_BB_CFO_TRACKING	|
				ODM_BB_DYNAMIC_TXPWR	|
				ODM_BB_NHM_CNT;
			break;
			
		case ODM_RTL8822B:
			support_ability |=
				ODM_BB_DIG				|
				ODM_BB_FA_CNT			|
				ODM_BB_CCK_PD			|
				ODM_BB_CFO_TRACKING	|
				ODM_BB_RATE_ADAPTIVE	|
				ODM_BB_RSSI_MONITOR	|
				ODM_BB_RA_MASK		|
				ODM_RF_TX_PWR_TRACK	|
				ODM_MAC_EDCA_TURBO;
			break;
		
		case ODM_RTL8821C:
			support_ability |= 
				ODM_BB_DIG 			|
				ODM_BB_RA_MASK		|				
				ODM_BB_CCK_PD			|
				ODM_BB_FA_CNT			|
				ODM_BB_RSSI_MONITOR	|
				ODM_BB_RATE_ADAPTIVE	|
				ODM_RF_TX_PWR_TRACK	|
				ODM_MAC_EDCA_TURBO	|
				ODM_BB_CFO_TRACKING;	//|
/*				ODM_BB_DYNAMIC_TXPWR	|*/
/*				ODM_BB_NHM_CNT;*/
			break;
		default:
			DbgPrint("[Warning] Supportability Init error !!!\n");
			break;
			
	}

	if(*(pDM_Odm->p_enable_antdiv))
		support_ability |= ODM_BB_ANT_DIV;

	if (*(pDM_Odm->p_enable_adaptivity)) {
		
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ODM Adaptivity is set to Enabled!!!\n"));
		
		support_ability |= ODM_BB_ADAPTIVITY;

		#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
		phydm_adaptivityInfoInit(pDM_Odm, PHYDM_ADAPINFO_CARRIER_SENSE_ENABLE, pMgntInfo->RegEnableCarrierSense);
		phydm_adaptivityInfoInit(pDM_Odm, PHYDM_ADAPINFO_DCBACKOFF, pMgntInfo->RegDCbackoff);
		phydm_adaptivityInfoInit(pDM_Odm, PHYDM_ADAPINFO_DYNAMICLINKADAPTIVITY, pMgntInfo->RegDmLinkAdaptivity);
		phydm_adaptivityInfoInit(pDM_Odm, PHYDM_ADAPINFO_AP_NUM_TH, pMgntInfo->RegAPNumTH);
		phydm_adaptivityInfoInit(pDM_Odm, PHYDM_ADAPINFO_TH_L2H_INI, pMgntInfo->RegL2HForAdaptivity);
		phydm_adaptivityInfoInit(pDM_Odm, PHYDM_ADAPINFO_TH_EDCCA_HL_DIFF, pMgntInfo->RegHLDiffForAdaptivity);
		#endif
	} else {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ODM Adaptivity is set to disnabled!!!\n"));
		/**/
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("PHYDM support_ability = ((0x%x))\n", support_ability));
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_ABILITY, support_ability);
}
#endif

//
// 2011/09/21 MH Add to describe different team necessary resource allocate??
//
VOID
ODM_DMInit(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#endif

	#if SUPPORTABLITY_PHYDMLIZE
	phydm_supportability_Init(pDM_Odm);
	#endif
	odm_CommonInfoSelfInit(pDM_Odm);
	odm_DIGInit(pDM_Odm);
	Phydm_NHMCounterStatisticsInit(pDM_Odm);
	Phydm_AdaptivityInit(pDM_Odm);
	phydm_ra_info_init(pDM_Odm);
	odm_RateAdaptiveMaskInit(pDM_Odm);
	ODM_CfoTrackingInit(pDM_Odm);
	ODM_EdcaTurboInit(pDM_Odm);
	odm_RSSIMonitorInit(pDM_Odm);
	phydm_rf_init(pDM_Odm);
	odm_TXPowerTrackingInit(pDM_Odm);
	odm_AntennaDiversityInit(pDM_Odm);
	odm_AutoChannelSelectInit(pDM_Odm);
	odm_PathDiversityInit(pDM_Odm);
	odm_DynamicTxPowerInit(pDM_Odm);
	phydm_initRaInfo(pDM_Odm);
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (PHYDM_LA_MODE_SUPPORT == 1)			
	ADCSmp_Init(pDM_Odm);
#endif
#endif
	
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#ifdef BEAMFORMING_VERSION_1
	if (pHalData->BeamformingVersion == BEAMFORMING_VERSION_1)
#endif
	{
		phydm_Beamforming_Init(pDM_Odm);
	}
#endif	

	if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
	{
#if (defined(CONFIG_BB_POWER_SAVING))
		odm_DynamicBBPowerSavingInit(pDM_Odm);
#endif

#if (RTL8188E_SUPPORT == 1)
		if(pDM_Odm->SupportICType==ODM_RTL8188E)
		{
			ODM_PrimaryCCA_Init(pDM_Odm);
			ODM_RAInfo_Init_all(pDM_Odm);
		}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	
	#if (RTL8723B_SUPPORT == 1)
		if(pDM_Odm->SupportICType == ODM_RTL8723B)
			odm_SwAntDetectInit(pDM_Odm);
	#endif

	#if (RTL8192E_SUPPORT == 1)
		if(pDM_Odm->SupportICType==ODM_RTL8192E)
			odm_PrimaryCCA_Check_Init(pDM_Odm);
	#endif

#endif

	}

}

VOID
ODM_DMReset(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;

	ODM_AntDivReset(pDM_Odm);	
	phydm_setEDCCAThresholdAPI(pDM_Odm, pDM_DigTable->CurIGValue);
}


VOID
phydm_support_ability_debug(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		*const dm_value,
	IN		u4Byte			*_used,
	OUT		char			*output,
	IN		u4Byte			*_out_len
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			pre_support_ability;
	u4Byte used = *_used;
	u4Byte out_len = *_out_len;

	pre_support_ability = pDM_Odm->SupportAbility ;	
	PHYDM_SNPRINTF((output+used, out_len-used,"\n%s\n", "================================"));
	if(dm_value[0] == 100)
	{
		PHYDM_SNPRINTF((output+used, out_len-used, "[Supportability] PhyDM Selection\n"));
		PHYDM_SNPRINTF((output+used, out_len-used,"%s\n", "================================"));
		PHYDM_SNPRINTF((output+used, out_len-used, "00. (( %s ))DIG\n", ((pDM_Odm->SupportAbility & ODM_BB_DIG)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "01. (( %s ))RA_MASK\n", ((pDM_Odm->SupportAbility & ODM_BB_RA_MASK)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "02. (( %s ))DYNAMIC_TXPWR\n", ((pDM_Odm->SupportAbility & ODM_BB_DYNAMIC_TXPWR)?("V"):("."))));		
		PHYDM_SNPRINTF((output+used, out_len-used, "03. (( %s ))FA_CNT\n", ((pDM_Odm->SupportAbility & ODM_BB_FA_CNT)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "04. (( %s ))RSSI_MONITOR\n", ((pDM_Odm->SupportAbility & ODM_BB_RSSI_MONITOR)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "05. (( %s ))CCK_PD\n", ((pDM_Odm->SupportAbility & ODM_BB_CCK_PD)?("V"):("."))));	
		PHYDM_SNPRINTF((output+used, out_len-used, "06. (( %s ))ANT_DIV\n", ((pDM_Odm->SupportAbility & ODM_BB_ANT_DIV)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "08. (( %s ))PWR_TRAIN\n", ((pDM_Odm->SupportAbility & ODM_BB_PWR_TRAIN)?("V"):("."))));	
		PHYDM_SNPRINTF((output+used, out_len-used, "09. (( %s ))RATE_ADAPTIVE\n", ((pDM_Odm->SupportAbility & ODM_BB_RATE_ADAPTIVE)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "10. (( %s ))PATH_DIV\n", ((pDM_Odm->SupportAbility & ODM_BB_PATH_DIV)?("V"):(".")))); 
		PHYDM_SNPRINTF((output+used, out_len-used, "13. (( %s ))ADAPTIVITY\n", ((pDM_Odm->SupportAbility & ODM_BB_ADAPTIVITY)?("V"):("."))));	
		PHYDM_SNPRINTF((output+used, out_len-used, "14. (( %s ))CFO_TRACKING\n", ((pDM_Odm->SupportAbility & ODM_BB_CFO_TRACKING)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "15. (( %s ))NHM_CNT\n", ((pDM_Odm->SupportAbility & ODM_BB_NHM_CNT)?("V"):("."))));	
		PHYDM_SNPRINTF((output+used, out_len-used, "16. (( %s ))PRIMARY_CCA\n", ((pDM_Odm->SupportAbility & ODM_BB_PRIMARY_CCA)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "17. (( %s ))TXBF\n", ((pDM_Odm->SupportAbility & ODM_BB_TXBF)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "18. (( %s ))DYNAMIC_ARFR\n", ((pDM_Odm->SupportAbility & ODM_BB_DYNAMIC_ARFR)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "20. (( %s ))EDCA_TURBO\n", ((pDM_Odm->SupportAbility & ODM_MAC_EDCA_TURBO)?("V"):("."))));	
		PHYDM_SNPRINTF((output+used, out_len-used, "21. (( %s ))EARLY_MODE\n", ((pDM_Odm->SupportAbility & ODM_MAC_EARLY_MODE)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "24. (( %s ))TX_PWR_TRACK\n", ((pDM_Odm->SupportAbility & ODM_RF_TX_PWR_TRACK)?("V"):("."))));	
		PHYDM_SNPRINTF((output+used, out_len-used, "25. (( %s ))RX_GAIN_TRACK\n", ((pDM_Odm->SupportAbility & ODM_RF_RX_GAIN_TRACK)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used, "26. (( %s ))RF_CALIBRATION\n", ((pDM_Odm->SupportAbility & ODM_RF_CALIBRATION)?("V"):("."))));
		PHYDM_SNPRINTF((output+used, out_len-used,"%s\n", "================================"));
	}
	/*
	else if(dm_value[0] == 101)
	{
		pDM_Odm->SupportAbility = 0 ;
		DbgPrint("Disable all SupportAbility components \n");
		PHYDM_SNPRINTF((output+used, out_len-used,"%s\n", "Disable all SupportAbility components"));	
	}
	*/
	else
	{

		if(dm_value[1] == 1) //enable
		{
			pDM_Odm->SupportAbility |= BIT(dm_value[0]) ;
			if(BIT(dm_value[0]) & ODM_BB_PATH_DIV)
			{
				odm_PathDiversityInit(pDM_Odm);
			}
		}
		else if(dm_value[1] == 2) //disable
		{
			pDM_Odm->SupportAbility &= ~(BIT(dm_value[0])) ;
		}
		else
		{
			//DbgPrint("\n[Warning!!!]  1:enable,  2:disable \n\n");
			PHYDM_SNPRINTF((output+used, out_len-used,"%s\n", "[Warning!!!]  1:enable,  2:disable"));
		}
	}
	PHYDM_SNPRINTF((output+used, out_len-used,"pre-SupportAbility  =  0x%x\n",  pre_support_ability ));	
	PHYDM_SNPRINTF((output+used, out_len-used,"Curr-SupportAbility =  0x%x\n", pDM_Odm->SupportAbility ));
	PHYDM_SNPRINTF((output+used, out_len-used,"%s\n", "================================"));
}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
//
//tmp modify for LC Only
//
VOID
ODM_DMWatchdog_LPS(
	IN		PDM_ODM_T		pDM_Odm
	)
{	
	odm_CommonInfoSelfUpdate(pDM_Odm);
	odm_FalseAlarmCounterStatistics(pDM_Odm);
	odm_RSSIMonitorCheck(pDM_Odm);
	odm_DIGbyRSSI_LPS(pDM_Odm);	
	odm_CCKPacketDetectionThresh(pDM_Odm);
	odm_CommonInfoSelfReset(pDM_Odm);

	if(*(pDM_Odm->pbPowerSaving)==TRUE)
		return;
}
#endif
//
// 2011/09/20 MH This is the entry pointer for all team to execute HW out source DM.
// You can not add any dummy function here, be care, you can only use DM structure
// to perform any new ODM_DM.
//
VOID
ODM_DMWatchdog(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	odm_CommonInfoSelfUpdate(pDM_Odm);
	phydm_BasicDbgMessage(pDM_Odm);
	odm_HWSetting(pDM_Odm);

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	{
	prtl8192cd_priv priv		= pDM_Odm->priv;
	if( (priv->auto_channel != 0) && (priv->auto_channel != 2) )//if ACS running, do not do FA/CCA counter read
		return;
	}
#endif	
	odm_FalseAlarmCounterStatistics(pDM_Odm);
	phydm_NoisyDetection(pDM_Odm);
	
	odm_RSSIMonitorCheck(pDM_Odm);

	if(*(pDM_Odm->pbPowerSaving) == TRUE)
	{
		odm_DIGbyRSSI_LPS(pDM_Odm);
		{
			pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;
			Phydm_Adaptivity(pDM_Odm, pDM_DigTable->CurIGValue);
		}
		#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
		odm_AntennaDiversity(pDM_Odm); /*enable AntDiv in PS mode, request from SD4 Jeff*/
		#endif
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("DMWatchdog in power saving mode\n"));
		return;
	}
	
	Phydm_CheckAdaptivity(pDM_Odm);
	odm_UpdatePowerTrainingState(pDM_Odm);
	odm_DIG(pDM_Odm);
	{
		pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;
		Phydm_Adaptivity(pDM_Odm, pDM_DigTable->CurIGValue);
	}
	odm_CCKPacketDetectionThresh(pDM_Odm);
	
	phydm_ra_info_watchdog(pDM_Odm);
	odm_EdcaTurboCheck(pDM_Odm);
	odm_PathDiversity(pDM_Odm);
	ODM_CfoTracking(pDM_Odm);
	odm_DynamicTxPower(pDM_Odm);
	odm_AntennaDiversity(pDM_Odm);
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	phydm_Beamforming_Watchdog(pDM_Odm);
#endif

	phydm_rf_watchdog(pDM_Odm);

	if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
	{
	        
#if (RTL8188E_SUPPORT == 1)
		if (pDM_Odm->SupportICType == ODM_RTL8188E)
			ODM_DynamicPrimaryCCA(pDM_Odm);
#endif

#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))

	#if (RTL8192E_SUPPORT == 1)
		if(pDM_Odm->SupportICType==ODM_RTL8192E)
			odm_DynamicPrimaryCCA_Check(pDM_Odm); 
	#endif
#endif
	}

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	odm_dtc(pDM_Odm);
#endif

	odm_CommonInfoSelfReset(pDM_Odm);
	
}


//
// Init /.. Fixed HW value. Only init time.
//
VOID
ODM_CmnInfoInit(
	IN		PDM_ODM_T		pDM_Odm,
	IN		ODM_CMNINFO_E	CmnInfo,
	IN		u4Byte			Value	
	)
{
	//
	// This section is used for init value
	//
	switch	(CmnInfo)
	{
		//
		// Fixed ODM value.
		//
		case	ODM_CMNINFO_ABILITY:
			pDM_Odm->SupportAbility = (u4Byte)Value;
			break;

		case	ODM_CMNINFO_RF_TYPE:
			pDM_Odm->RFType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_PLATFORM:
			pDM_Odm->SupportPlatform = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_INTERFACE:
			pDM_Odm->SupportInterface = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_MP_TEST_CHIP:
			pDM_Odm->bIsMPChip= (u1Byte)Value;
			break;
            
		case	ODM_CMNINFO_IC_TYPE:
			pDM_Odm->SupportICType = Value;
			break;

		case	ODM_CMNINFO_CUT_VER:
			pDM_Odm->CutVersion = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_FAB_VER:
			pDM_Odm->FabVersion = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_RFE_TYPE:
			pDM_Odm->RFEType = (u1Byte)Value;
			PHYDM_InitHwInfoByRfe(pDM_Odm);
			break;

		case    ODM_CMNINFO_RF_ANTENNA_TYPE:
			pDM_Odm->AntDivType= (u1Byte)Value;
			break;
			
		case	ODM_CMNINFO_WITH_EXT_ANTENNA_SWITCH:
			pDM_Odm->with_extenal_ant_switch = (u1Byte)Value;
			break;	
			
		case    ODM_CMNINFO_BE_FIX_TX_ANT:
			pDM_Odm->DM_FatTable.b_fix_tx_ant = (u1Byte)Value;
			break;	

		case	ODM_CMNINFO_BOARD_TYPE:
			if (!pDM_Odm->bInitHwInfoByRfe)
				pDM_Odm->BoardType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_PACKAGE_TYPE:
			if (!pDM_Odm->bInitHwInfoByRfe)
				pDM_Odm->PackageType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_EXT_LNA:
			if (!pDM_Odm->bInitHwInfoByRfe)
				pDM_Odm->ExtLNA = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_5G_EXT_LNA:
			if (!pDM_Odm->bInitHwInfoByRfe)
				pDM_Odm->ExtLNA5G = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_EXT_PA:
			if (!pDM_Odm->bInitHwInfoByRfe)
				pDM_Odm->ExtPA = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_5G_EXT_PA:
			if (!pDM_Odm->bInitHwInfoByRfe)
				pDM_Odm->ExtPA5G = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_GPA:
			if (!pDM_Odm->bInitHwInfoByRfe)
				pDM_Odm->TypeGPA = (u2Byte)Value;
			break;
			
		case	ODM_CMNINFO_APA:
			if (!pDM_Odm->bInitHwInfoByRfe)
				pDM_Odm->TypeAPA = (u2Byte)Value;
			break;
			
		case	ODM_CMNINFO_GLNA:
			if (!pDM_Odm->bInitHwInfoByRfe)
				pDM_Odm->TypeGLNA = (u2Byte)Value;
			break;
			
		case	ODM_CMNINFO_ALNA:
			if (!pDM_Odm->bInitHwInfoByRfe)
				pDM_Odm->TypeALNA = (u2Byte)Value;
			break;

		case	ODM_CMNINFO_EXT_TRSW:
			pDM_Odm->ExtTRSW = (u1Byte)Value;
			break;
		case	ODM_CMNINFO_EXT_LNA_GAIN:
			pDM_Odm->ExtLNAGain = (u1Byte)Value;
			break;
		case 	ODM_CMNINFO_PATCH_ID:
			pDM_Odm->PatchID = (u1Byte)Value;
			break;
		case 	ODM_CMNINFO_BINHCT_TEST:
			pDM_Odm->bInHctTest = (BOOLEAN)Value;
			break;
		case 	ODM_CMNINFO_BWIFI_TEST:
			pDM_Odm->WIFITest = (u1Byte)Value;
			break;	
		case	ODM_CMNINFO_SMART_CONCURRENT:
			pDM_Odm->bDualMacSmartConcurrent = (BOOLEAN )Value;
			break;
		case	ODM_CMNINFO_DOMAIN_CODE_2G:
			pDM_Odm->odm_Regulation2_4G = (u1Byte)Value;
			break;
		case	ODM_CMNINFO_DOMAIN_CODE_5G:
			pDM_Odm->odm_Regulation5G = (u1Byte)Value;
			break;
		case	ODM_CMNINFO_CONFIG_BB_RF:
			pDM_Odm->ConfigBBRF = (BOOLEAN)Value;
			break;
		case	ODM_CMNINFO_IQKFWOFFLOAD:
			pDM_Odm->IQKFWOffload = (u1Byte)Value;
			break;
		case	ODM_CMNINFO_IQKPAOFF:
			pDM_Odm->RFCalibrateInfo.bIQKPAoff = (BOOLEAN )Value;
			break;
		case	ODM_CMNINFO_REGRFKFREEENABLE:
			pDM_Odm->RFCalibrateInfo.RegRfKFreeEnable = (u1Byte)Value;
			break;
		case	ODM_CMNINFO_RFKFREEENABLE:
			pDM_Odm->RFCalibrateInfo.RfKFreeEnable = (u1Byte)Value;
			break;
		case	ODM_CMNINFO_NORMAL_RX_PATH_CHANGE:
			pDM_Odm->Normalrxpath = (u1Byte)Value;
			break;
#ifdef CONFIG_PHYDM_DFS_MASTER
		case	ODM_CMNINFO_DFS_REGION_DOMAIN:
			pDM_Odm->DFS_RegionDomain = (u1Byte)Value;
			break;
#endif
		//To remove the compiler warning, must add an empty default statement to handle the other values.	
		default:
			//do nothing
			break;	
		
	}

}


VOID
ODM_CmnInfoHook(
	IN		PDM_ODM_T		pDM_Odm,
	IN		ODM_CMNINFO_E	CmnInfo,
	IN		PVOID			pValue	
	)
{
	//
	// Hook call by reference pointer.
	//
	switch	(CmnInfo)
	{
		//
		// Dynamic call by reference pointer.
		//
		case	ODM_CMNINFO_MAC_PHY_MODE:
			pDM_Odm->pMacPhyMode = (u1Byte *)pValue;
			break;
		
		case	ODM_CMNINFO_TX_UNI:
			pDM_Odm->pNumTxBytesUnicast = (u8Byte *)pValue;
			break;

		case	ODM_CMNINFO_RX_UNI:
			pDM_Odm->pNumRxBytesUnicast = (u8Byte *)pValue;
			break;

		case	ODM_CMNINFO_WM_MODE:
			pDM_Odm->pWirelessMode = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_BAND:
			pDM_Odm->pBandType = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_SEC_CHNL_OFFSET:
			pDM_Odm->pSecChOffset = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_SEC_MODE:
			pDM_Odm->pSecurity = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_BW:
			pDM_Odm->pBandWidth = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_CHNL:
			pDM_Odm->pChannel = (u1Byte *)pValue;
			break;
		
		case	ODM_CMNINFO_DMSP_GET_VALUE:
			pDM_Odm->pbGetValueFromOtherMac = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_BUDDY_ADAPTOR:
			pDM_Odm->pBuddyAdapter = (PADAPTER *)pValue;
			break;

		case	ODM_CMNINFO_DMSP_IS_MASTER:
			pDM_Odm->pbMasterOfDMSP = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_SCAN:
			pDM_Odm->pbScanInProcess = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_POWER_SAVING:
			pDM_Odm->pbPowerSaving = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_ONE_PATH_CCA:
			pDM_Odm->pOnePathCCA = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_DRV_STOP:
			pDM_Odm->pbDriverStopped =  (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_PNP_IN:
			pDM_Odm->pbDriverIsGoingToPnpSetPowerSleep =  (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_INIT_ON:
			pDM_Odm->pinit_adpt_in_progress =  (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_ANT_TEST:
			pDM_Odm->pAntennaTest =  (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_NET_CLOSED:
			pDM_Odm->pbNet_closed = (BOOLEAN *)pValue;
			break;

		case 	ODM_CMNINFO_FORCED_RATE:
			pDM_Odm->pForcedDataRate = (pu2Byte)pValue;
			break;
		case	ODM_CMNINFO_ANT_DIV:
			pDM_Odm->p_enable_antdiv = (pu1Byte)pValue;
			break;
		case	ODM_CMNINFO_ADAPTIVITY:
			pDM_Odm->p_enable_adaptivity = (pu1Byte)pValue;
			break;
		case  ODM_CMNINFO_FORCED_IGI_LB:
			pDM_Odm->pu1ForcedIgiLb = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_P2P_LINK:
			pDM_Odm->DM_DigTable.bP2PInProcess = (u1Byte *)pValue;
			break;

		case 	ODM_CMNINFO_IS1ANTENNA:
			pDM_Odm->pIs1Antenna = (BOOLEAN *)pValue;
			break;
			
		case 	ODM_CMNINFO_RFDEFAULTPATH:
			pDM_Odm->pRFDefaultPath= (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_FCS_MODE:
			pDM_Odm->pIsFcsModeEnable = (BOOLEAN *)pValue;
			break;
		/*add by YuChen for beamforming PhyDM*/
		case	ODM_CMNINFO_HUBUSBMODE:
			pDM_Odm->HubUsbMode = (u1Byte *)pValue;
			break;
		case	ODM_CMNINFO_FWDWRSVDPAGEINPROGRESS:
			pDM_Odm->pbFwDwRsvdPageInProgress = (BOOLEAN *)pValue;
			break;
		case	ODM_CMNINFO_TX_TP:
			pDM_Odm->pCurrentTxTP = (u4Byte *)pValue;
			break;
		case	ODM_CMNINFO_RX_TP:
			pDM_Odm->pCurrentRxTP = (u4Byte *)pValue;
			break;
		case	ODM_CMNINFO_SOUNDING_SEQ:
			pDM_Odm->pSoundingSeq = (u1Byte *)pValue;
			break;
#ifdef CONFIG_PHYDM_DFS_MASTER
		case	ODM_CMNINFO_DFS_MASTER_ENABLE:
			pDM_Odm->dfs_master_enabled = (u1Byte *)pValue;
			break;
#endif
		case	ODM_CMNINFO_FORCE_TX_ANT_BY_TXDESC:
			pDM_Odm->DM_FatTable.pForceTxAntByDesc = (u1Byte *)pValue;
			break;
		case	ODM_CMNINFO_SET_S0S1_DEFAULT_ANTENNA:
			pDM_Odm->DM_FatTable.pDefaultS0S1 = (u1Byte *)pValue;
			break;
		
		default:
			/*do nothing*/
			break;

	}

}


VOID
ODM_CmnInfoPtrArrayHook(
	IN		PDM_ODM_T		pDM_Odm,
	IN		ODM_CMNINFO_E	CmnInfo,
	IN		u2Byte			Index,
	IN		PVOID			pValue	
	)
{
	/*Hook call by reference pointer.*/
	switch	(CmnInfo)
	{
		/*Dynamic call by reference pointer.	*/
		case	ODM_CMNINFO_STA_STATUS:
			pDM_Odm->pODM_StaInfo[Index] = (PSTA_INFO_T)pValue;
			
			if (IS_STA_VALID(pDM_Odm->pODM_StaInfo[Index]))
			#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
				pDM_Odm->platform2phydm_macid_table[((PSTA_INFO_T)pValue)->AssociatedMacId] = Index; /*AssociatedMacId are unique bttween different Adapter*/
			#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
				pDM_Odm->platform2phydm_macid_table[((PSTA_INFO_T)pValue)->aid] = Index;
			#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
				pDM_Odm->platform2phydm_macid_table[((PSTA_INFO_T)pValue)->mac_id] = Index;
			#endif
			
			break;		
		//To remove the compiler warning, must add an empty default statement to handle the other values.				
		default:
			//do nothing
			break;
	}
	
}


//
// Update Band/CHannel/.. The values are dynamic but non-per-packet.
//
VOID
ODM_CmnInfoUpdate(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u4Byte			CmnInfo,
	IN		u8Byte			Value	
	)
{
	//
	// This init variable may be changed in run time.
	//
	switch	(CmnInfo)
	{
		case ODM_CMNINFO_LINK_IN_PROGRESS:
			pDM_Odm->bLinkInProcess = (BOOLEAN)Value;
			break;
		
		case	ODM_CMNINFO_ABILITY:
			pDM_Odm->SupportAbility = (u4Byte)Value;
			break;

		case	ODM_CMNINFO_RF_TYPE:
			pDM_Odm->RFType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_WIFI_DIRECT:
			pDM_Odm->bWIFI_Direct = (BOOLEAN)Value;
			break;

		case	ODM_CMNINFO_WIFI_DISPLAY:
			pDM_Odm->bWIFI_Display = (BOOLEAN)Value;
			break;

		case	ODM_CMNINFO_LINK:
			pDM_Odm->bLinked = (BOOLEAN)Value;
			break;

		case	ODM_CMNINFO_STATION_STATE:
			pDM_Odm->bsta_state = (BOOLEAN)Value;
			break;
			
		case	ODM_CMNINFO_RSSI_MIN:
			pDM_Odm->RSSI_Min= (u1Byte)Value;
			break;

		case	ODM_CMNINFO_DBG_COMP:
			pDM_Odm->DebugComponents = (u4Byte)Value;
			break;

		case	ODM_CMNINFO_DBG_LEVEL:
			pDM_Odm->DebugLevel = (u4Byte)Value;
			break;
		case	ODM_CMNINFO_RA_THRESHOLD_HIGH:
			pDM_Odm->RateAdaptive.HighRSSIThresh = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_RA_THRESHOLD_LOW:
			pDM_Odm->RateAdaptive.LowRSSIThresh = (u1Byte)Value;
			break;
#if defined(BT_SUPPORT) && (BT_SUPPORT == 1)
		// The following is for BT HS mode and BT coexist mechanism.
		case ODM_CMNINFO_BT_ENABLED:
			pDM_Odm->bBtEnabled = (BOOLEAN)Value;
			break;
			
		case ODM_CMNINFO_BT_HS_CONNECT_PROCESS:
			pDM_Odm->bBtConnectProcess = (BOOLEAN)Value;
			break;
		
		case ODM_CMNINFO_BT_HS_RSSI:
			pDM_Odm->btHsRssi = (u1Byte)Value;
			break;
			
		case	ODM_CMNINFO_BT_OPERATION:
			pDM_Odm->bBtHsOperation = (BOOLEAN)Value;
			break;

		case	ODM_CMNINFO_BT_LIMITED_DIG:
			pDM_Odm->bBtLimitedDig = (BOOLEAN)Value;
			break;	

		case ODM_CMNINFO_BT_DIG:
			pDM_Odm->btHsDigVal = (u1Byte)Value;
			break;
			
		case	ODM_CMNINFO_BT_BUSY:
			pDM_Odm->bBtBusy = (BOOLEAN)Value;
			break;	

		case	ODM_CMNINFO_BT_DISABLE_EDCA:
			pDM_Odm->bBtDisableEdcaTurbo = (BOOLEAN)Value;
			break;
#endif

#if(DM_ODM_SUPPORT_TYPE & ODM_AP)		// for repeater mode add by YuChen 2014.06.23
#ifdef UNIVERSAL_REPEATER
		case	ODM_CMNINFO_VXD_LINK:
			pDM_Odm->VXD_bLinked= (BOOLEAN)Value;
			break;
#endif
#endif

		case	ODM_CMNINFO_AP_TOTAL_NUM:
			pDM_Odm->APTotalNum = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_POWER_TRAINING:
			pDM_Odm->bDisablePowerTraining = (BOOLEAN)Value;
			break;

#ifdef CONFIG_PHYDM_DFS_MASTER
		case	ODM_CMNINFO_DFS_REGION_DOMAIN:
			pDM_Odm->DFS_RegionDomain = (u1Byte)Value;
			break;
#endif

/*
		case	ODM_CMNINFO_OP_MODE:
			pDM_Odm->OPMode = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_WM_MODE:
			pDM_Odm->WirelessMode = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_BAND:
			pDM_Odm->BandType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_SEC_CHNL_OFFSET:
			pDM_Odm->SecChOffset = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_SEC_MODE:
			pDM_Odm->Security = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_BW:
			pDM_Odm->BandWidth = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_CHNL:
			pDM_Odm->Channel = (u1Byte)Value;
			break;			
*/	
                default:
			//do nothing
			break;
	}

	
}

u4Byte
PHYDM_CmnInfoQuery(
	IN		PDM_ODM_T					pDM_Odm,
	IN		PHYDM_INFO_QUERY_E			info_type
	)
{
	PFALSE_ALARM_STATISTICS	FalseAlmCnt = (PFALSE_ALARM_STATISTICS)PhyDM_Get_Structure(pDM_Odm, PHYDM_FALSEALMCNT);

	switch (info_type) {
	case PHYDM_INFO_FA_OFDM:
		return FalseAlmCnt->Cnt_Ofdm_fail;
			
	case PHYDM_INFO_FA_CCK:
		return FalseAlmCnt->Cnt_Cck_fail;

	case PHYDM_INFO_FA_TOTAL:
		return FalseAlmCnt->Cnt_all;

	case PHYDM_INFO_CCA_OFDM:
		return FalseAlmCnt->Cnt_OFDM_CCA;

	case PHYDM_INFO_CCA_CCK:
		return FalseAlmCnt->Cnt_CCK_CCA;

	case PHYDM_INFO_CCA_ALL:
		return FalseAlmCnt->Cnt_CCA_all;

	case PHYDM_INFO_CRC32_OK_VHT:
		return FalseAlmCnt->cnt_vht_crc32_ok;

	case PHYDM_INFO_CRC32_OK_HT:
		return FalseAlmCnt->cnt_ht_crc32_ok;

	case PHYDM_INFO_CRC32_OK_LEGACY:
		return FalseAlmCnt->cnt_ofdm_crc32_ok;

	case PHYDM_INFO_CRC32_OK_CCK:
		return FalseAlmCnt->cnt_cck_crc32_ok;

	case PHYDM_INFO_CRC32_ERROR_VHT:
		return FalseAlmCnt->cnt_vht_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_HT:
		return FalseAlmCnt->cnt_ht_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_LEGACY:
		return FalseAlmCnt->cnt_ofdm_crc32_error;

	case PHYDM_INFO_CRC32_ERROR_CCK:
		return FalseAlmCnt->cnt_cck_crc32_error;

	case PHYDM_INFO_EDCCA_FLAG:
		return FalseAlmCnt->edcca_flag;

	case PHYDM_INFO_OFDM_ENABLE:
		return FalseAlmCnt->ofdm_block_enable;

	case PHYDM_INFO_CCK_ENABLE:
		return FalseAlmCnt->cck_block_enable;

	case PHYDM_INFO_DBG_PORT_0:
		return FalseAlmCnt->dbg_port0;

	default:
		return 0xffffffff;
		
	}
}


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
ODM_InitAllWorkItems(IN PDM_ODM_T	pDM_Odm )
{

	PADAPTER		pAdapter = pDM_Odm->Adapter;
#if USE_WORKITEM
	#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
	ODM_InitializeWorkItem(	pDM_Odm, 
							&pDM_Odm->DM_SWAT_Table.phydm_SwAntennaSwitchWorkitem, 
							(RT_WORKITEM_CALL_BACK)ODM_SW_AntDiv_WorkitemCallback,
							(PVOID)pAdapter,
							"AntennaSwitchWorkitem");
	#endif
	#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
	ODM_InitializeWorkItem(pDM_Odm, 
						&pDM_Odm->dm_sat_table.hl_smart_antenna_workitem, 
						(RT_WORKITEM_CALL_BACK)phydm_beam_switch_workitem_callback,
						(PVOID)pAdapter,
						"hl_smart_ant_workitem");

	ODM_InitializeWorkItem(pDM_Odm, 
						&pDM_Odm->dm_sat_table.hl_smart_antenna_decision_workitem, 
						(RT_WORKITEM_CALL_BACK)phydm_beam_decision_workitem_callback,
						(PVOID)pAdapter,
						"hl_smart_ant_decision_workitem");
	#endif
	
	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->PathDivSwitchWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_PathDivChkAntSwitchWorkitemCallback, 
		(PVOID)pAdapter,
		"SWAS_WorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->CCKPathDiversityWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_CCKTXPathDiversityWorkItemCallback, 
		(PVOID)pAdapter,
		"CCKTXPathDiversityWorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->MPT_DIGWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_MPT_DIGWorkItemCallback, 
		(PVOID)pAdapter,
		"MPT_DIGWorkitem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->RaRptWorkitem), 
		(RT_WORKITEM_CALL_BACK)ODM_UpdateInitRateWorkItemCallback, 
		(PVOID)pAdapter,
		"RaRptWorkitem");

#if( defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY) ) ||( defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY) )
	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->FastAntTrainingWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_FastAntTrainingWorkItemCallback, 
		(PVOID)pAdapter,
		"FastAntTrainingWorkitem");
#endif

#endif /*#if USE_WORKITEM*/

#if (BEAMFORMING_SUPPORT == 1)
	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_EnterWorkItem),
		(RT_WORKITEM_CALL_BACK)halComTxbf_EnterWorkItemCallback,
		(PVOID)pAdapter,
		"Txbf_EnterWorkItem");
	
	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_LeaveWorkItem),
		(RT_WORKITEM_CALL_BACK)halComTxbf_LeaveWorkItemCallback,
		(PVOID)pAdapter,
		"Txbf_LeaveWorkItem");
	
	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_FwNdpaWorkItem),
		(RT_WORKITEM_CALL_BACK)halComTxbf_FwNdpaWorkItemCallback,
		(PVOID)pAdapter,
		"Txbf_FwNdpaWorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_ClkWorkItem),
		(RT_WORKITEM_CALL_BACK)halComTxbf_ClkWorkItemCallback,
		(PVOID)pAdapter,
		"Txbf_ClkWorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_RateWorkItem),
		(RT_WORKITEM_CALL_BACK)halComTxbf_RateWorkItemCallback,
		(PVOID)pAdapter,
		"Txbf_RateWorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_StatusWorkItem),
		(RT_WORKITEM_CALL_BACK)halComTxbf_StatusWorkItemCallback,
		(PVOID)pAdapter,
		"Txbf_StatusWorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_ResetTxPathWorkItem),
		(RT_WORKITEM_CALL_BACK)halComTxbf_ResetTxPathWorkItemCallback,
		(PVOID)pAdapter,
		"Txbf_ResetTxPathWorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_GetTxRateWorkItem),
		(RT_WORKITEM_CALL_BACK)halComTxbf_GetTxRateWorkItemCallback,
		(PVOID)pAdapter,
		"Txbf_GetTxRateWorkItem");
#endif

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->Adaptivity.phydm_pauseEDCCAWorkItem),
		(RT_WORKITEM_CALL_BACK)phydm_pauseEDCCA_WorkItemCallback,
		(PVOID)pAdapter,
		"phydm_pauseEDCCAWorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->Adaptivity.phydm_resumeEDCCAWorkItem),
		(RT_WORKITEM_CALL_BACK)phydm_resumeEDCCA_WorkItemCallback,
		(PVOID)pAdapter,
		"phydm_resumeEDCCAWorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm, 
		&(pDM_Odm->adcsmp.ADCSmpWorkItem), 
		(RT_WORKITEM_CALL_BACK)ADCSmpWorkItemCallback, 
		(PVOID)pAdapter, 
		"ADCSmpWorkItem");

}

VOID
ODM_FreeAllWorkItems(IN PDM_ODM_T	pDM_Odm )
{
#if USE_WORKITEM

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
	ODM_FreeWorkItem(&(pDM_Odm->DM_SWAT_Table.phydm_SwAntennaSwitchWorkitem));
#endif

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
	ODM_FreeWorkItem(&(pDM_Odm->dm_sat_table.hl_smart_antenna_workitem));
	ODM_FreeWorkItem(&(pDM_Odm->dm_sat_table.hl_smart_antenna_decision_workitem));
#endif

	ODM_FreeWorkItem(&(pDM_Odm->PathDivSwitchWorkitem));      
	ODM_FreeWorkItem(&(pDM_Odm->CCKPathDiversityWorkitem));
#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
	ODM_FreeWorkItem(&(pDM_Odm->FastAntTrainingWorkitem));
#endif
	ODM_FreeWorkItem(&(pDM_Odm->MPT_DIGWorkitem));
	ODM_FreeWorkItem(&(pDM_Odm->RaRptWorkitem));
	/*ODM_FreeWorkItem((&pDM_Odm->sbdcnt_workitem));*/
#endif

#if (BEAMFORMING_SUPPORT == 1)
	ODM_FreeWorkItem((&pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_EnterWorkItem));
	ODM_FreeWorkItem((&pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_LeaveWorkItem));
	ODM_FreeWorkItem((&pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_FwNdpaWorkItem));
	ODM_FreeWorkItem((&pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_ClkWorkItem));
	ODM_FreeWorkItem((&pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_RateWorkItem));
	ODM_FreeWorkItem((&pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_StatusWorkItem));
	ODM_FreeWorkItem((&pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_ResetTxPathWorkItem));
	ODM_FreeWorkItem((&pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_GetTxRateWorkItem));
#endif

	ODM_FreeWorkItem((&pDM_Odm->Adaptivity.phydm_pauseEDCCAWorkItem));
	ODM_FreeWorkItem((&pDM_Odm->Adaptivity.phydm_resumeEDCCAWorkItem));
	ODM_FreeWorkItem((&pDM_Odm->adcsmp.ADCSmpWorkItem));

}
#endif /*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/

/*
VOID
odm_FindMinimumRSSI(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	u4Byte	i;
	u1Byte	RSSI_Min = 0xFF;

	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
//		if(pDM_Odm->pODM_StaInfo[i] != NULL)
		if(IS_STA_VALID(pDM_Odm->pODM_StaInfo[i]) )
		{
			if(pDM_Odm->pODM_StaInfo[i]->RSSI_Ave < RSSI_Min)
			{
				RSSI_Min = pDM_Odm->pODM_StaInfo[i]->RSSI_Ave;
			}
		}
	}

	pDM_Odm->RSSI_Min = RSSI_Min;

}

VOID
odm_IsLinked(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	u4Byte i;
	BOOLEAN Linked = FALSE;
	
	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
			if(IS_STA_VALID(pDM_Odm->pODM_StaInfo[i]) )
			{			
				Linked = TRUE;
				break;
			}
		
	}

	pDM_Odm->bLinked = Linked;
}
*/

VOID
ODM_InitAllTimers(
	IN PDM_ODM_T	pDM_Odm 
	)
{
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	ODM_AntDivTimers(pDM_Odm,INIT_ANTDIV_TIMMER);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#ifdef MP_TEST
	if (pDM_Odm->priv->pshare->rf_ft_var.mp_specific) 
		ODM_InitializeTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer, 
			(RT_TIMER_CALL_BACK)odm_MPT_DIGCallback, NULL, "MPT_DIGTimer");	
#endif
#elif(DM_ODM_SUPPORT_TYPE == ODM_WIN)
	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer, 
		(RT_TIMER_CALL_BACK)odm_MPT_DIGCallback, NULL, "MPT_DIGTimer");
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 
		(RT_TIMER_CALL_BACK)odm_PathDivChkAntSwitchCallback, NULL, "PathDivTimer");
	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->CCKPathDiversityTimer, 
		(RT_TIMER_CALL_BACK)odm_CCKTXPathDiversityCallback, NULL, "CCKPathDiversityTimer"); 
	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->sbdcnt_timer,
		(RT_TIMER_CALL_BACK)phydm_sbd_callback, NULL, "SbdTimer"); 
#if (BEAMFORMING_SUPPORT == 1)
	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_FwNdpaTimer,
		(RT_TIMER_CALL_BACK)halComTxbf_FwNdpaTimerCallback, NULL, "Txbf_FwNdpaTimer");
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (BEAMFORMING_SUPPORT == 1)
	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->BeamformingInfo.BeamformingTimer,
		(RT_TIMER_CALL_BACK)Beamforming_SWTimerCallback, NULL, "BeamformingTimer");
#endif
#endif
}

VOID
ODM_CancelAllTimers(
	IN PDM_ODM_T	pDM_Odm 
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	//
	// 2012/01/12 MH Temp BSOD fix. We need to find NIC allocate mem fail reason in 
	// win7 platform.
	//
	HAL_ADAPTER_STS_CHK(pDM_Odm)
#endif	

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	ODM_AntDivTimers(pDM_Odm,CANCEL_ANTDIV_TIMMER);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#ifdef MP_TEST
	if (pDM_Odm->priv->pshare->rf_ft_var.mp_specific)
		ODM_CancelTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer);
#endif
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->PathDivSwitchTimer);
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->CCKPathDiversityTimer);
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer);
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->sbdcnt_timer);
#if (BEAMFORMING_SUPPORT == 1)
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_FwNdpaTimer);
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (BEAMFORMING_SUPPORT == 1)
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->BeamformingInfo.BeamformingTimer);
#endif
#endif

}


VOID
ODM_ReleaseAllTimers(
	IN PDM_ODM_T	pDM_Odm 
	)
{
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	ODM_AntDivTimers(pDM_Odm,RELEASE_ANTDIV_TIMMER);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
    #ifdef MP_TEST
	if (pDM_Odm->priv->pshare->rf_ft_var.mp_specific)
		ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer);
    #endif
#elif(DM_ODM_SUPPORT_TYPE == ODM_WIN)
ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->PathDivSwitchTimer);
	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->CCKPathDiversityTimer);
	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer);
	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->sbdcnt_timer);
#if (BEAMFORMING_SUPPORT == 1)
	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->BeamformingInfo.TxbfInfo.Txbf_FwNdpaTimer);
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (BEAMFORMING_SUPPORT == 1)
	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->BeamformingInfo.BeamformingTimer);
#endif
#endif
}


//3============================================================
//3 Tx Power Tracking
//3============================================================




#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
VOID
ODM_InitAllThreads(
	IN PDM_ODM_T	pDM_Odm 
	)
{
	#ifdef TPT_THREAD
	kTPT_task_init(pDM_Odm->priv);
	#endif
}

VOID
ODM_StopAllThreads(
	IN PDM_ODM_T	pDM_Odm 
	)
{
	#ifdef TPT_THREAD
	kTPT_task_stop(pDM_Odm->priv);
	#endif
}
#endif	


#if( DM_ODM_SUPPORT_TYPE == ODM_WIN) 
//
// 2011/07/26 MH Add an API for testing IQK fail case.
//
BOOLEAN
ODM_CheckPowerStatus(
	IN	PADAPTER		Adapter)
{

	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T			pDM_Odm = &pHalData->DM_OutSrc;
	RT_RF_POWER_STATE 	rtState;
	PMGNT_INFO			pMgntInfo	= &(Adapter->MgntInfo);

	// 2011/07/27 MH We are not testing ready~~!! We may fail to get correct value when init sequence.
	if (pMgntInfo->init_adpt_in_progress == TRUE)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ODM_CheckPowerStatus Return TRUE, due to initadapter\n"));
		return	TRUE;
	}
	
	//
	//	2011/07/19 MH We can not execute tx pwoer tracking/ LLC calibrate or IQK.
	//
	Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));	
	if(Adapter->bDriverStopped || Adapter->bDriverIsGoingToPnpSetPowerSleep || rtState == eRfOff)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ODM_CheckPowerStatus Return FALSE, due to %d/%d/%d\n", 
		Adapter->bDriverStopped, Adapter->bDriverIsGoingToPnpSetPowerSleep, rtState));
		return	FALSE;
	}
	return	TRUE;
}
#elif( DM_ODM_SUPPORT_TYPE == ODM_AP)
BOOLEAN
ODM_CheckPowerStatus(
		IN	PADAPTER		Adapter)
{
	/*
	   HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	   PDM_ODM_T			pDM_Odm = &pHalData->DM_OutSrc;
	   RT_RF_POWER_STATE 	rtState;
	   PMGNT_INFO			pMgntInfo	= &(Adapter->MgntInfo);

	// 2011/07/27 MH We are not testing ready~~!! We may fail to get correct value when init sequence.
	if (pMgntInfo->init_adpt_in_progress == TRUE)
	{
	ODM_RT_TRACE(pDM_Odm,COMP_INIT, DBG_LOUD, ("ODM_CheckPowerStatus Return TRUE, due to initadapter"));
	return	TRUE;
	}

	//
	//	2011/07/19 MH We can not execute tx pwoer tracking/ LLC calibrate or IQK.
	//
	Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));	
	if(Adapter->bDriverStopped || Adapter->bDriverIsGoingToPnpSetPowerSleep || rtState == eRfOff)
	{
	ODM_RT_TRACE(pDM_Odm,COMP_INIT, DBG_LOUD, ("ODM_CheckPowerStatus Return FALSE, due to %d/%d/%d\n", 
	Adapter->bDriverStopped, Adapter->bDriverIsGoingToPnpSetPowerSleep, rtState));
	return	FALSE;
	}
	 */
	return	TRUE;
}
#endif

// need to ODM CE Platform
//move to here for ANT detection mechanism using

#if ((DM_ODM_SUPPORT_TYPE == ODM_WIN)||(DM_ODM_SUPPORT_TYPE == ODM_CE))
u4Byte
GetPSDData(
	IN PDM_ODM_T	pDM_Odm,
	unsigned int 	point,
	u1Byte initial_gain_psd)
{
	//unsigned int	val, rfval;
	//int	psd_report;
	u4Byte	psd_report;
	
	//HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//Debug Message
	//val = PHY_QueryBBReg(Adapter,0x908, bMaskDWord);
	//DbgPrint("Reg908 = 0x%x\n",val);
	//val = PHY_QueryBBReg(Adapter,0xDF4, bMaskDWord);
	//rfval = PHY_QueryRFReg(Adapter, ODM_RF_PATH_A, 0x00, bRFRegOffsetMask);
	//DbgPrint("RegDF4 = 0x%x, RFReg00 = 0x%x\n",val, rfval);
	//DbgPrint("PHYTXON = %x, OFDMCCA_PP = %x, CCKCCA_PP = %x, RFReg00 = %x\n",
		//(val&BIT25)>>25, (val&BIT14)>>14, (val&BIT15)>>15, rfval);

	//Set DCO frequency index, offset=(40MHz/SamplePts)*point
	ODM_SetBBReg(pDM_Odm, 0x808, 0x3FF, point);

	//Start PSD calculation, Reg808[22]=0->1
	ODM_SetBBReg(pDM_Odm, 0x808, BIT22, 1);
	//Need to wait for HW PSD report
	ODM_StallExecution(1000);
	ODM_SetBBReg(pDM_Odm, 0x808, BIT22, 0);
	//Read PSD report, Reg8B4[15:0]
	psd_report = ODM_GetBBReg(pDM_Odm,0x8B4, bMaskDWord) & 0x0000FFFF;
	
#if 1//(DEV_BUS_TYPE == RT_PCI_INTERFACE) && ( (RT_PLATFORM == PLATFORM_LINUX) || (RT_PLATFORM == PLATFORM_MACOSX))
	psd_report = (u4Byte) (odm_ConvertTo_dB(psd_report))+(u4Byte)(initial_gain_psd-0x1c);
#else
	psd_report = (int) (20*log10((double)psd_report))+(int)(initial_gain_psd-0x1c);
#endif

	return psd_report;
	
}
#endif

u4Byte 
odm_ConvertTo_dB(
	u4Byte 	Value)
{
	u1Byte i;
	u1Byte j;
	u4Byte dB;

	Value = Value & 0xFFFF;

	for (i = 0; i < 12; i++)
	{
		if (Value <= dB_Invert_Table[i][7])
		{
			break;
		}
	}

	if (i >= 12)
	{
		return (96);	// maximum 96 dB
	}

	for (j = 0; j < 8; j++)
	{
		if (Value <= dB_Invert_Table[i][j])
		{
			break;
		}
	}

	dB = (i << 3) + j + 1;

	return (dB);
}

u4Byte 
odm_ConvertTo_linear(
	u4Byte 	Value)
{
	u1Byte i;
	u1Byte j;
	u4Byte linear;
	
	/* 1dB~96dB */
	
	Value = Value & 0xFF;

	i = (u1Byte)((Value - 1) >> 3);
	j = (u1Byte)(Value - 1) - (i << 3);

	linear = dB_Invert_Table[i][j];

	return (linear);
}

//
// ODM multi-port consideration, added by Roger, 2013.10.01.
//
VOID
ODM_AsocEntry_Init(
	IN	PDM_ODM_T	pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER pLoopAdapter = GetDefaultAdapter(pDM_Odm->Adapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pLoopAdapter);
	PDM_ODM_T		 pDM_OutSrc = &pHalData->DM_OutSrc;
	u1Byte	TotalAssocEntryNum = 0;
	u1Byte	index = 0;
	u1Byte	adaptercount = 0;

	ODM_CmnInfoPtrArrayHook(pDM_OutSrc, ODM_CMNINFO_STA_STATUS, 0, &pLoopAdapter->MgntInfo.DefaultPort[0]);
	pLoopAdapter->MgntInfo.DefaultPort[0].MultiPortStationIdx = TotalAssocEntryNum;
		
	adaptercount += 1;
	RT_TRACE(COMP_INIT, DBG_LOUD, ("adaptercount=%d\n", adaptercount));	
	pLoopAdapter = GetNextExtAdapter(pLoopAdapter);
	TotalAssocEntryNum +=1;

	while(pLoopAdapter)
	{
		for (index = 0; index <ASSOCIATE_ENTRY_NUM; index++)
		{
			ODM_CmnInfoPtrArrayHook(pDM_OutSrc, ODM_CMNINFO_STA_STATUS, TotalAssocEntryNum+index, &pLoopAdapter->MgntInfo.AsocEntry[index]);
			pLoopAdapter->MgntInfo.AsocEntry[index].MultiPortStationIdx = TotalAssocEntryNum+index;				
		}
		
		TotalAssocEntryNum+= index;
		if(IS_HARDWARE_TYPE_8188E((pDM_Odm->Adapter)))
			pLoopAdapter->RASupport = TRUE;
		adaptercount += 1;
		RT_TRACE(COMP_INIT, DBG_LOUD, ("adaptercount=%d\n", adaptercount));
		pLoopAdapter = GetNextExtAdapter(pLoopAdapter);
	}

	RT_TRACE(COMP_INIT, DBG_LOUD, ("TotalAssocEntryNum = %d\n", TotalAssocEntryNum));
	if (TotalAssocEntryNum < (ODM_ASSOCIATE_ENTRY_NUM-1)) {
	
		RT_TRACE(COMP_INIT, DBG_LOUD, ("In hook null\n"));
		for (index = TotalAssocEntryNum; index < ODM_ASSOCIATE_ENTRY_NUM; index++)
			ODM_CmnInfoPtrArrayHook(pDM_OutSrc, ODM_CMNINFO_STA_STATUS, index, NULL);
	}
#endif
}

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
/* Justin: According to the current RRSI to adjust Response Frame TX power, 2012/11/05 */
void odm_dtc(PDM_ODM_T pDM_Odm)
{
#ifdef CONFIG_DM_RESP_TXAGC
	#define DTC_BASE            35	/* RSSI higher than this value, start to decade TX power */
	#define DTC_DWN_BASE       (DTC_BASE-5)	/* RSSI lower than this value, start to increase TX power */

	/* RSSI vs TX power step mapping: decade TX power */
	static const u8 dtc_table_down[]={
		DTC_BASE,
		(DTC_BASE+5),
		(DTC_BASE+10),
		(DTC_BASE+15),
		(DTC_BASE+20),
		(DTC_BASE+25)
	};

	/* RSSI vs TX power step mapping: increase TX power */
	static const u8 dtc_table_up[]={
		DTC_DWN_BASE,
		(DTC_DWN_BASE-5),
		(DTC_DWN_BASE-10),
		(DTC_DWN_BASE-15),
		(DTC_DWN_BASE-15),
		(DTC_DWN_BASE-20),
		(DTC_DWN_BASE-20),
		(DTC_DWN_BASE-25),
		(DTC_DWN_BASE-25),
		(DTC_DWN_BASE-30),
		(DTC_DWN_BASE-35)
	};

	u8 i;
	u8 dtc_steps=0;
	u8 sign;
	u8 resp_txagc=0;

	#if 0
	/* As DIG is disabled, DTC is also disable */
	if(!(pDM_Odm->SupportAbility & ODM_XXXXXX))
		return;
	#endif

	if (DTC_BASE < pDM_Odm->RSSI_Min) {
		/* need to decade the CTS TX power */
		sign = 1;
		for (i=0;i<ARRAY_SIZE(dtc_table_down);i++)
		{
			if ((dtc_table_down[i] >= pDM_Odm->RSSI_Min) || (dtc_steps >= 6))
				break;
			else
				dtc_steps++;
		}
	}
#if 0
	else if (DTC_DWN_BASE > pDM_Odm->RSSI_Min)
	{
		/* needs to increase the CTS TX power */
		sign = 0;
		dtc_steps = 1;
		for (i=0;i<ARRAY_SIZE(dtc_table_up);i++)
		{
			if ((dtc_table_up[i] <= pDM_Odm->RSSI_Min) || (dtc_steps>=10))
				break;
			else
				dtc_steps++;
		}
	}
#endif
	else
	{
		sign = 0;
		dtc_steps = 0;
	}

	resp_txagc = dtc_steps | (sign << 4);
	resp_txagc = resp_txagc | (resp_txagc << 5);
	ODM_Write1Byte(pDM_Odm, 0x06d9, resp_txagc);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_PWR_TRAIN, ODM_DBG_LOUD, ("%s RSSI_Min:%u, set RESP_TXAGC to %s %u\n",
		__func__, pDM_Odm->RSSI_Min, sign ? "minus" : "plus", dtc_steps));
#endif /* CONFIG_RESP_TXAGC_ADJUST */
}

#endif /* #if (DM_ODM_SUPPORT_TYPE == ODM_CE) */

VOID
odm_UpdatePowerTrainingState(
	IN	PDM_ODM_T	pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PFALSE_ALARM_STATISTICS 	FalseAlmCnt = (PFALSE_ALARM_STATISTICS)PhyDM_Get_Structure( pDM_Odm , PHYDM_FALSEALMCNT);
	pDIG_T						pDM_DigTable = &pDM_Odm->DM_DigTable;
	u4Byte						score = 0;

	if(!(pDM_Odm->SupportAbility & ODM_BB_PWR_TRAIN))
		return;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState()============>\n"));
	pDM_Odm->bChangeState = FALSE;

	// Debug command
	if(pDM_Odm->ForcePowerTrainingState)
	{
		if(pDM_Odm->ForcePowerTrainingState == 1 && !pDM_Odm->bDisablePowerTraining)
		{
			pDM_Odm->bChangeState = TRUE;
			pDM_Odm->bDisablePowerTraining = TRUE;
		}
		else if(pDM_Odm->ForcePowerTrainingState == 2 && pDM_Odm->bDisablePowerTraining)
		{
			pDM_Odm->bChangeState = TRUE;
			pDM_Odm->bDisablePowerTraining = FALSE;
		}

		pDM_Odm->PT_score = 0;
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM = 0;
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK = 0;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): ForcePowerTrainingState = %d\n", 
			pDM_Odm->ForcePowerTrainingState));
		return;
	}
	
	if(!pDM_Odm->bLinked)
		return;
	
	// First connect
	if((pDM_Odm->bLinked) && (pDM_DigTable->bMediaConnect_0 == FALSE))
	{
		pDM_Odm->PT_score = 0;
		pDM_Odm->bChangeState = TRUE;
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM = 0;
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK = 0;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): First Connect\n"));
		return;
	}

	// Compute score
	if(pDM_Odm->NHM_cnt_0 >= 215)
		score = 2;
	else if(pDM_Odm->NHM_cnt_0 >= 190) 
		score = 1;							// unknow state
	else
	{
		u4Byte	RX_Pkt_Cnt;
		
		RX_Pkt_Cnt = (u4Byte)(pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM) + (u4Byte)(pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK);
		
		if((FalseAlmCnt->Cnt_CCA_all > 31 && RX_Pkt_Cnt > 31) && (FalseAlmCnt->Cnt_CCA_all >= RX_Pkt_Cnt))
		{
			if((RX_Pkt_Cnt + (RX_Pkt_Cnt >> 1)) <= FalseAlmCnt->Cnt_CCA_all)
				score = 0;
			else if((RX_Pkt_Cnt + (RX_Pkt_Cnt >> 2)) <= FalseAlmCnt->Cnt_CCA_all)
				score = 1;
			else
				score = 2;
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): RX_Pkt_Cnt = %d, Cnt_CCA_all = %d\n", 
			RX_Pkt_Cnt, FalseAlmCnt->Cnt_CCA_all));
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): NumQryPhyStatusOFDM = %d, NumQryPhyStatusCCK = %d\n",
			(u4Byte)(pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM), (u4Byte)(pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK)));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): NHM_cnt_0 = %d, score = %d\n", 
		pDM_Odm->NHM_cnt_0, score));

	// smoothing
	pDM_Odm->PT_score = (score << 4) + (pDM_Odm->PT_score>>1) + (pDM_Odm->PT_score>>2);
	score = (pDM_Odm->PT_score + 32) >> 6;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): PT_score = %d, score after smoothing = %d\n", 
		pDM_Odm->PT_score, score));

	// Mode decision
	if(score == 2)
	{
		if(pDM_Odm->bDisablePowerTraining)
		{
			pDM_Odm->bChangeState = TRUE;
			pDM_Odm->bDisablePowerTraining = FALSE;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): Change state\n"));
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): Enable Power Training\n"));
	}
	else if(score == 0)
	{
		if(!pDM_Odm->bDisablePowerTraining)
		{
			pDM_Odm->bChangeState = TRUE;
			pDM_Odm->bDisablePowerTraining = TRUE;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): Change state\n"));
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_RA_MASK, ODM_DBG_LOUD,("odm_UpdatePowerTrainingState(): Disable Power Training\n"));
	}

	pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM = 0;
	pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK = 0;
#endif
}



/*===========================================================*/
/* The following is for compile only*/
/*===========================================================*/
/*#define TARGET_CHNL_NUM_2G_5G	59*/
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

u1Byte GetRightChnlPlaceforIQK(u1Byte chnl)
{
	u1Byte	channel_all[TARGET_CHNL_NUM_2G_5G] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 100, 
		102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126, 128, 130, 132, 134, 136, 138, 140, 149, 151, 153, 155, 157, 159, 161, 163, 165};
	u1Byte	place = chnl;

	
	if (chnl > 14) {
		for (place = 14; place < sizeof(channel_all); place++) {
			if (channel_all[place] == chnl)
				return place-13;
		}
	}
	
	return 0;
}

#endif
/*===========================================================*/

VOID
phydm_NoisyDetection(
	IN	PDM_ODM_T	pDM_Odm
	)
{
	u4Byte  Total_FA_Cnt, Total_CCA_Cnt;
	u4Byte  Score = 0, i, Score_Smooth;
    
	Total_CCA_Cnt = pDM_Odm->FalseAlmCnt.Cnt_CCA_all;
	Total_FA_Cnt  = pDM_Odm->FalseAlmCnt.Cnt_all;    

/*
    if( Total_FA_Cnt*16>=Total_CCA_Cnt*14 )         // 87.5
    
    else if( Total_FA_Cnt*16>=Total_CCA_Cnt*12 )    // 75
    
    else if( Total_FA_Cnt*16>=Total_CCA_Cnt*10 )    // 56.25
    
    else if( Total_FA_Cnt*16>=Total_CCA_Cnt*8 )     // 50

    else if( Total_FA_Cnt*16>=Total_CCA_Cnt*7 )     // 43.75

    else if( Total_FA_Cnt*16>=Total_CCA_Cnt*6 )     // 37.5

    else if( Total_FA_Cnt*16>=Total_CCA_Cnt*5 )     // 31.25%
        
    else if( Total_FA_Cnt*16>=Total_CCA_Cnt*4 )     // 25%

    else if( Total_FA_Cnt*16>=Total_CCA_Cnt*3 )     // 18.75%

    else if( Total_FA_Cnt*16>=Total_CCA_Cnt*2 )     // 12.5%

    else if( Total_FA_Cnt*16>=Total_CCA_Cnt*1 )     // 6.25%
*/
    for(i=0;i<=16;i++)
    {
        if( Total_FA_Cnt*16>=Total_CCA_Cnt*(16-i) )
        {
            Score = 16-i;
            break;
        }
    }

    // NoisyDecision_Smooth = NoisyDecision_Smooth>>1 + (Score<<3)>>1;
    pDM_Odm->NoisyDecision_Smooth = (pDM_Odm->NoisyDecision_Smooth>>1) + (Score<<2);

    // Round the NoisyDecision_Smooth: +"3" comes from (2^3)/2-1
    Score_Smooth = (Total_CCA_Cnt>=300)?((pDM_Odm->NoisyDecision_Smooth+3)>>3):0;

    pDM_Odm->NoisyDecision = (Score_Smooth>=3)?1:0;
/*
    switch(Score_Smooth)
    {
        case 0:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=0%%\n"));
            break;
        case 1:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=6.25%%\n"));
            break;
        case 2:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=12.5%%\n"));
            break;
        case 3:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=18.75%%\n"));
            break;
        case 4:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=25%%\n"));
            break;
        case 5:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=31.25%%\n"));
            break;
        case 6:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=37.5%%\n"));
            break;
        case 7:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=43.75%%\n"));
            break;
        case 8:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=50%%\n"));
            break;
        case 9:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=56.25%%\n"));
            break;
        case 10:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=62.5%%\n"));
            break;
        case 11:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=68.75%%\n"));
            break;
        case 12:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=75%%\n"));
            break;
        case 13:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=81.25%%\n"));
            break;
        case 14:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=87.5%%\n"));
            break;
        case 15:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=93.75%%\n"));            
            break;
        case 16:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Total_FA_Cnt/Total_CCA_Cnt=100%%\n"));
            break;
        default:
            ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,
            ("[NoisyDetection] Unknown Value!! Need Check!!\n"));            
    }
*/        
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_NOISY_DETECT, ODM_DBG_LOUD,
	("[NoisyDetection] Total_CCA_Cnt=%d, Total_FA_Cnt=%d, NoisyDecision_Smooth=%d, Score=%d, Score_Smooth=%d, pDM_Odm->NoisyDecision=%d\n",
	Total_CCA_Cnt, Total_FA_Cnt, pDM_Odm->NoisyDecision_Smooth, Score, Score_Smooth, pDM_Odm->NoisyDecision));
	
}

VOID
phydm_set_ext_switch(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		*const dm_value,
	IN		u4Byte		*_used,
	OUT		char			*output,
	IN		u4Byte		*_out_len	
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			used = *_used;
	u4Byte			out_len = *_out_len;
	u4Byte			ext_ant_switch =  dm_value[0];

	if (pDM_Odm->SupportICType & (ODM_RTL8821|ODM_RTL8881A)) {
		
		/*Output Pin Settings*/
		ODM_SetMACReg(pDM_Odm, 0x4C, BIT23, 0); /*select DPDT_P and DPDT_N as output pin*/
		ODM_SetMACReg(pDM_Odm, 0x4C, BIT24, 1); /*by WLAN control*/
		
		ODM_SetBBReg(pDM_Odm, 0xCB4, 0xF, 7); /*DPDT_P = 1b'0*/
		ODM_SetBBReg(pDM_Odm, 0xCB4, 0xF0, 7); /*DPDT_N = 1b'0*/

		if (ext_ant_switch == MAIN_ANT) {
			ODM_SetBBReg(pDM_Odm, 0xCB4, (BIT29|BIT28), 1);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("***8821A set Ant switch = 2b'01 (Main)\n"));
		} else if (ext_ant_switch == AUX_ANT){
			ODM_SetBBReg(pDM_Odm, 0xCB4, BIT29|BIT28, 2);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("***8821A set Ant switch = 2b'10 (Aux)\n"));
		}
	}
}

VOID
phydm_csi_mask_enable(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		enable
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte		reg_value = 0;

	reg_value = (enable == CSI_MASK_ENABLE) ? 1 : 0;
	
	if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {

		ODM_SetBBReg(pDM_Odm, 0xD2C, BIT28, reg_value);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("Enable CSI Mask:  Reg 0xD2C[28] = ((0x%x))\n", reg_value));
		
	} else if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {

		ODM_SetBBReg(pDM_Odm, 0x874, BIT0, reg_value);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("Enable CSI Mask:  Reg 0x874[0] = ((0x%x))\n", reg_value));
	}
	
}

VOID
phydm_clean_all_csi_mask(
	IN		PVOID		pDM_VOID
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {
		
		ODM_SetBBReg(pDM_Odm, 0xD40, bMaskDWord, 0);
		ODM_SetBBReg(pDM_Odm, 0xD44, bMaskDWord, 0);
		ODM_SetBBReg(pDM_Odm, 0xD48, bMaskDWord, 0);
		ODM_SetBBReg(pDM_Odm, 0xD4c, bMaskDWord, 0);
		
	} else if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {
	
		ODM_SetBBReg(pDM_Odm, 0x880, bMaskDWord, 0);
		ODM_SetBBReg(pDM_Odm, 0x884, bMaskDWord, 0);
		ODM_SetBBReg(pDM_Odm, 0x888, bMaskDWord, 0);
		ODM_SetBBReg(pDM_Odm, 0x88c, bMaskDWord, 0);
		ODM_SetBBReg(pDM_Odm, 0x890, bMaskDWord, 0);
		ODM_SetBBReg(pDM_Odm, 0x894, bMaskDWord, 0);
		ODM_SetBBReg(pDM_Odm, 0x898, bMaskDWord, 0);
		ODM_SetBBReg(pDM_Odm, 0x89c, bMaskDWord, 0);
	}
}

VOID
phydm_set_csi_mask_reg(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		tone_idx_tmp,
	IN		u1Byte		tone_direction
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte		byte_offset, bit_offset;
	u4Byte		target_reg;
	u1Byte		reg_tmp_value;
	u4Byte		tone_num = 64;
	u4Byte		tone_num_shift = 0;
	u4Byte		csi_mask_reg_p = 0, csi_mask_reg_n = 0;

	/* calculate real tone idx*/
	if ((tone_idx_tmp % 10) >= 5)
		tone_idx_tmp += 10;

	tone_idx_tmp = (tone_idx_tmp/10);

	if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {

		tone_num = 64;
		csi_mask_reg_p = 0xD40;
		csi_mask_reg_n = 0xD48;		
		
	} else if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {

		tone_num = 128;
		csi_mask_reg_p = 0x880;
		csi_mask_reg_n = 0x890;	
	}
	
	if (tone_direction == FREQ_POSITIVE) {

		if (tone_idx_tmp >= (tone_num - 1))
			tone_idx_tmp = (tone_num - 1);
		
		byte_offset = (u1Byte)(tone_idx_tmp >> 3);
		bit_offset = (u1Byte)(tone_idx_tmp & 0x7);
		target_reg = csi_mask_reg_p + byte_offset;
		
	} else {
		tone_num_shift = tone_num;
	
		if (tone_idx_tmp >= tone_num)
			tone_idx_tmp = tone_num;

		tone_idx_tmp = tone_num - tone_idx_tmp;
		
		byte_offset = (u1Byte)(tone_idx_tmp >> 3);
		bit_offset = (u1Byte)(tone_idx_tmp & 0x7);			
		target_reg = csi_mask_reg_n + byte_offset;
	}
	
	reg_tmp_value = ODM_Read1Byte(pDM_Odm, target_reg);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("Pre Mask tone idx[%d]:  Reg0x%x = ((0x%x))\n", (tone_idx_tmp + tone_num_shift), target_reg, reg_tmp_value));
	reg_tmp_value |= BIT(bit_offset);
	ODM_Write1Byte(pDM_Odm, target_reg, reg_tmp_value);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("New Mask tone idx[%d]:  Reg0x%x = ((0x%x))\n", (tone_idx_tmp + tone_num_shift), target_reg, reg_tmp_value));
}

VOID
phydm_set_nbi_reg(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		tone_idx_tmp,
	IN		u4Byte		bw
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte	nbi_table_128[NBI_TABLE_SIZE_128] = {25, 55, 85, 115, 135, 155, 185, 205, 225, 245,		/*1~10*/		/*tone_idx X 10*/
												265, 285, 305, 335, 355, 375, 395, 415, 435, 455,	/*11~20*/
												485, 505, 525, 555, 585, 615, 635};				/*21~27*/
	
	u4Byte	nbi_table_256[NBI_TABLE_SIZE_256] = { 25,   55,   85, 115, 135, 155, 175, 195, 225, 245,	/*1~10*/
												265, 285, 305, 325, 345, 365, 385, 405, 425, 445,	/*11~20*/
												465, 485, 505, 525, 545, 565, 585, 605, 625, 645,	/*21~30*/
												665, 695, 715, 735, 755, 775, 795, 815, 835, 855,	/*31~40*/
												875, 895, 915, 935, 955, 975, 995, 1015, 1035, 1055,	/*41~50*/
												1085, 1105, 1125, 1145, 1175, 1195, 1225, 1255, 1275};	/*51~59*/

	u4Byte	reg_idx = 0;
	u4Byte	i;
	u1Byte	nbi_table_idx = FFT_128_TYPE;

	if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {
		
		nbi_table_idx = FFT_128_TYPE;
	} else if (pDM_Odm->SupportICType & ODM_IC_11AC_1_SERIES) {
	
		nbi_table_idx = FFT_256_TYPE;
	} else if (pDM_Odm->SupportICType & ODM_IC_11AC_2_SERIES) {
	
		if (bw == 80)
			nbi_table_idx = FFT_256_TYPE;
		else /*20M, 40M*/
			nbi_table_idx = FFT_128_TYPE;
	}

	if (nbi_table_idx == FFT_128_TYPE) {
		
		for (i = 0; i < NBI_TABLE_SIZE_128; i++) {
			if (tone_idx_tmp < nbi_table_128[i]) {
				reg_idx = i+1;
				break;
			}
		}
		
	} else if (nbi_table_idx == FFT_256_TYPE) {

		for (i = 0; i < NBI_TABLE_SIZE_256; i++) {
			if (tone_idx_tmp < nbi_table_256[i]) {
				reg_idx = i+1;
				break;
			}
		}	
	}

	if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {
		ODM_SetBBReg(pDM_Odm, 0xc40, 0x1f000000, reg_idx);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("Set tone idx:  Reg0xC40[28:24] = ((0x%x))\n", reg_idx));
		/**/
	} else {
		ODM_SetBBReg(pDM_Odm, 0x87c, 0xfc000, reg_idx);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("Set tone idx: Reg0x87C[19:14] = ((0x%x))\n", reg_idx));
		/**/
	}
}


VOID
phydm_nbi_enable(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		enable
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte		reg_value = 0;

	reg_value = (enable == NBI_ENABLE) ? 1 : 0;
	
	if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {

		ODM_SetBBReg(pDM_Odm, 0xc40, BIT9, reg_value);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("Enable NBI Reg0xC40[9] = ((0x%x))\n", reg_value));
		
	} else if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {

		ODM_SetBBReg(pDM_Odm, 0x87c, BIT13, reg_value);
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("Enable NBI Reg0x87C[13] = ((0x%x))\n", reg_value));
	}
}

u1Byte
phydm_calculate_fc(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		channel,
	IN		u4Byte		bw,
	IN		u4Byte		Second_ch,
	IN OUT	u4Byte		*fc_in
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte		fc = *fc_in;
	u4Byte		start_ch_per_40m[NUM_START_CH_40M] = {36, 44, 52, 60, 100, 108, 116, 124, 132, 140, 149, 157, 165, 173};
	u4Byte		start_ch_per_80m[NUM_START_CH_80M] = {36, 52, 100, 116, 132, 149, 165}; 
	pu4Byte		p_start_ch = &(start_ch_per_40m[0]);
	u4Byte		num_start_channel = NUM_START_CH_40M;
	u4Byte		channel_offset = 0;
	u4Byte		i;

	/*2.4G*/
	if (channel <= 14 && channel > 0) {

		if (bw == 80) {
			return	SET_ERROR;
		}

		fc = 2412 + (channel - 1)*5;

		if (bw == 40 && (Second_ch == PHYDM_ABOVE)) {
			
			if (channel >= 10) {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("CH = ((%d)), Scnd_CH = ((%d)) Error Setting\n", channel, Second_ch));
				return	SET_ERROR;
			}
			fc += 10;
		} else if (bw == 40 && (Second_ch == PHYDM_BELOW)) {
		
			if (channel <= 2) {
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("CH = ((%d)), Scnd_CH = ((%d)) Error Setting\n", channel, Second_ch));
				return	SET_ERROR;
			}
			fc -= 10;
		}
	}
	/*5G*/
	else if (channel >= 36 && channel <= 177) {

		if (bw != 20) {
			
			if (bw == 40) {
				num_start_channel = NUM_START_CH_40M;
				p_start_ch = &(start_ch_per_40m[0]);
				channel_offset = CH_OFFSET_40M;
			} else if (bw == 80) {
				num_start_channel = NUM_START_CH_80M;
				p_start_ch = &(start_ch_per_80m[0]);
				channel_offset = CH_OFFSET_80M;
			}

			for (i = 0; i < num_start_channel; i++) {
				
				if (channel < p_start_ch[i+1]) {
					channel = p_start_ch[i] + channel_offset;
					break;
				}
			}
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("Mod_CH = ((%d))\n", channel));
		}
		
		fc = 5180 + (channel-36)*5;
		
	} else {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("CH = ((%d)) Error Setting\n", channel));
		return	SET_ERROR;
	}
	
	*fc_in = fc;
	
	return SET_SUCCESS;
}


u1Byte
phydm_calculate_intf_distance(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		bw,
	IN		u4Byte		fc,
	IN		u4Byte		f_interference,
	IN OUT	u4Byte		*p_tone_idx_tmp_in
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte		bw_up, bw_low;
	u4Byte		int_distance;
	u4Byte		tone_idx_tmp;
	u1Byte		set_result = SET_NO_NEED;
	
	bw_up = fc + bw/2;
	bw_low = fc - bw/2;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("[f_l, fc, fh] = [ %d, %d, %d ], f_int = ((%d))\n", bw_low, fc, bw_up, f_interference));

	if ((f_interference >= bw_low) && (f_interference <= bw_up)) {

		int_distance = (fc >= f_interference) ? (fc - f_interference) : (f_interference - fc);
		tone_idx_tmp = (int_distance<<5);  /* =10*(int_distance /0.3125) */
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("int_distance = ((%d MHz)) Mhz, tone_idx_tmp = ((%d.%d))\n", int_distance, (tone_idx_tmp/10), (tone_idx_tmp%10)));
		*p_tone_idx_tmp_in = tone_idx_tmp;
		set_result = SET_SUCCESS;
	}

	return	set_result;

}


u1Byte
phydm_csi_mask_setting(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		enable,
	IN		u4Byte		channel,
	IN		u4Byte		bw,
	IN		u4Byte		f_interference,
	IN		u4Byte		Second_ch
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte		fc;		
	u4Byte		int_distance;
	u1Byte		tone_direction;
	u4Byte		tone_idx_tmp;
	u1Byte		set_result = SET_SUCCESS;

	if (enable == CSI_MASK_DISABLE) {
		set_result = SET_SUCCESS;
		phydm_clean_all_csi_mask(pDM_Odm);
	
	} else {

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("[Set CSI MASK_] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n", 
			channel, bw, f_interference, (((bw == 20) || (channel > 14)) ? "Don't care" : (Second_ch == PHYDM_ABOVE) ? "H" : "L")));

		/*calculate fc*/
		if (phydm_calculate_fc(pDM_Odm, channel, bw, Second_ch, &fc) == SET_ERROR)
			set_result = SET_ERROR;
			
		else {
			/*calculate interference distance*/
			if (phydm_calculate_intf_distance(pDM_Odm, bw, fc, f_interference, &tone_idx_tmp) == SET_SUCCESS) {
				
				tone_direction = (f_interference >= fc) ? FREQ_POSITIVE : FREQ_NEGATIVE;
				phydm_set_csi_mask_reg(pDM_Odm, tone_idx_tmp, tone_direction);
				set_result = SET_SUCCESS;
			} else
				set_result = SET_NO_NEED;
		}
	}

	if (set_result == SET_SUCCESS)
		phydm_csi_mask_enable(pDM_Odm, enable);
	else
		phydm_csi_mask_enable(pDM_Odm, CSI_MASK_DISABLE);

	return	set_result;
}

u1Byte
phydm_nbi_setting(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		enable,
	IN		u4Byte		channel,
	IN		u4Byte		bw,
	IN		u4Byte		f_interference,
	IN		u4Byte		Second_ch
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte		fc;		
	u4Byte		int_distance;
	u4Byte		tone_idx_tmp;
	u1Byte		set_result = SET_SUCCESS;
	u4Byte		bw_max = 40;
	
	if (enable == NBI_DISABLE) 
		set_result = SET_SUCCESS;
	
	else {

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_API, ODM_DBG_LOUD, ("[Set NBI] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n", 
			channel, bw, f_interference, (((Second_ch == PHYDM_DONT_CARE) || (bw == 20) || (channel > 14)) ? "Don't care" : (Second_ch == PHYDM_ABOVE) ? "H" : "L")));			

		/*calculate fc*/
		if (phydm_calculate_fc(pDM_Odm, channel, bw, Second_ch, &fc) == SET_ERROR)
			set_result = SET_ERROR;
			
		else {
			/*calculate interference distance*/
			if (phydm_calculate_intf_distance(pDM_Odm, bw, fc, f_interference, &tone_idx_tmp) == SET_SUCCESS) {
				
				phydm_set_nbi_reg(pDM_Odm, tone_idx_tmp, bw);
				set_result = SET_SUCCESS;
			} else
				set_result = SET_NO_NEED;
		}
	}

	if (set_result == SET_SUCCESS)
		phydm_nbi_enable(pDM_Odm, enable);
	else
		phydm_nbi_enable(pDM_Odm, NBI_DISABLE);

	return	set_result;
}

VOID
phydm_api_debug(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		function_map,
	IN		u4Byte		*const dm_value,
	IN		u4Byte		*_used,
	OUT		char			*output,
	IN		u4Byte		*_out_len
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			used = *_used;
	u4Byte			out_len = *_out_len;
	u4Byte			channel =  dm_value[1];
	u4Byte			bw =  dm_value[2];
	u4Byte			f_interference =  dm_value[3];
	u4Byte			Second_ch =  dm_value[4];
	u1Byte			set_result = 0;

	/*PHYDM_API_NBI*/
	/*-------------------------------------------------------------------------------------------------------------------------------*/
	if (function_map == PHYDM_API_NBI) {
		
		if (dm_value[0] == 100) {
			
			PHYDM_SNPRINTF((output+used, out_len-used, "[HELP-NBI]  EN(on=1, off=2)   CH   BW(20/40/80)  f_intf(Mhz)    Scnd_CH(L=1, H=2)\n"));
			return;
			
		} else if (dm_value[0] == NBI_ENABLE) {
		
			PHYDM_SNPRINTF((output+used, out_len-used, "[Enable NBI] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n", 
				channel, bw, f_interference, ((Second_ch == PHYDM_DONT_CARE) || (bw == 20) || (channel > 14)) ? "Don't care" : ((Second_ch == PHYDM_ABOVE) ? "H" : "L")));
			set_result = phydm_nbi_setting(pDM_Odm, NBI_ENABLE, channel, bw, f_interference, Second_ch);
			
		} else if (dm_value[0] == NBI_DISABLE) {
		
			PHYDM_SNPRINTF((output+used, out_len-used, "[Disable NBI]\n"));
			set_result = phydm_nbi_setting(pDM_Odm, NBI_DISABLE, channel, bw, f_interference, Second_ch);
			
		} else {
		
			set_result = SET_ERROR;
		}
		PHYDM_SNPRINTF((output+used, out_len-used, "[NBI set result: %s]\n", (set_result == SET_SUCCESS) ? "Success" : ((set_result == SET_NO_NEED) ? "No need" : "Error")));
		
	} 
	
	/*PHYDM_CSI_MASK*/
	/*-------------------------------------------------------------------------------------------------------------------------------*/
	else if (function_map == PHYDM_API_CSI_MASK) {
		
		if (dm_value[0] == 100) {
			
			PHYDM_SNPRINTF((output+used, out_len-used, "[HELP-CSI MASK]  EN(on=1, off=2)   CH   BW(20/40/80)  f_intf(Mhz)    Scnd_CH(L=1, H=2)\n"));
			return;
			
		} else if (dm_value[0] == CSI_MASK_ENABLE) {
		
			PHYDM_SNPRINTF((output+used, out_len-used, "[Enable CSI MASK] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n", 
				channel, bw, f_interference, (channel > 14)?"Don't care":(((Second_ch == PHYDM_DONT_CARE) || (bw == 20) || (channel > 14)) ? "H" : "L")));
			set_result = phydm_csi_mask_setting(pDM_Odm,	CSI_MASK_ENABLE, channel, bw, f_interference, Second_ch);
			
		} else if (dm_value[0] == CSI_MASK_DISABLE) {
		
			PHYDM_SNPRINTF((output+used, out_len-used, "[Disable CSI MASK]\n"));
			set_result = phydm_csi_mask_setting(pDM_Odm, CSI_MASK_DISABLE, channel, bw, f_interference, Second_ch);
			
		} else {
		
			set_result = SET_ERROR;
		}
		PHYDM_SNPRINTF((output+used, out_len-used, "[CSI MASK set result: %s]\n", (set_result == SET_SUCCESS) ? "Success" : ((set_result == SET_NO_NEED) ? "No need" : "Error")));
	}
}


