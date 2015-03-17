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


#ifndef	__HALHWOUTSRC_H__
#define __HALHWOUTSRC_H__


/*--------------------------Define -------------------------------------------*/ 
//#define READ_NEXT_PAIR(v1, v2, i) do { i += 2; v1 = Array[i]; v2 = Array[i+1]; } while(0)
#define AGC_DIFF_CONFIG_MP(ic, band) (ODM_ReadAndConfig_MP_##ic##_AGC_TAB_DIFF(pDM_Odm, Array_MP_##ic##_AGC_TAB_DIFF_##band, \
                                                                              sizeof(Array_MP_##ic##_AGC_TAB_DIFF_##band)/sizeof(u4Byte)))
#define AGC_DIFF_CONFIG_TC(ic, band) (ODM_ReadAndConfig_TC_##ic##_AGC_TAB_DIFF(pDM_Odm, Array_TC_##ic##_AGC_TAB_DIFF_##band, \
                                                                              sizeof(Array_TC_##ic##_AGC_TAB_DIFF_##band)/sizeof(u4Byte)))

#define AGC_DIFF_CONFIG(ic, band) do {\
                                            if (pDM_Odm->bIsMPChip)\
                                    		    AGC_DIFF_CONFIG_MP(ic,band);\
                                            else\
                                                AGC_DIFF_CONFIG_TC(ic,band);\
                                    } while(0)


//============================================================
// structure and define
//============================================================

typedef struct _Phy_Rx_AGC_Info
{
	#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)	
		u1Byte	gain:7,trsw:1;			
	#else			
		u1Byte	trsw:1,gain:7;
	#endif
} PHY_RX_AGC_INFO_T,*pPHY_RX_AGC_INFO_T;

typedef struct _Phy_Status_Rpt_8192cd
{
	PHY_RX_AGC_INFO_T path_agc[2];
	u1Byte 	ch_corr[2];									
	u1Byte	cck_sig_qual_ofdm_pwdb_all;
	u1Byte	cck_agc_rpt_ofdm_cfosho_a;
	u1Byte	cck_rpt_b_ofdm_cfosho_b;
	u1Byte 	rsvd_1;//ch_corr_msb;
	u1Byte 	noise_power_db_msb;
	s1Byte	path_cfotail[2];	
	u1Byte	pcts_mask[2];	
	s1Byte	stream_rxevm[2];	
	u1Byte	path_rxsnr[2];
	u1Byte 	noise_power_db_lsb;
	u1Byte	rsvd_2[3];
	u1Byte 	stream_csi[2];
	u1Byte 	stream_target_csi[2];
	s1Byte 	sig_evm;
	u1Byte 	rsvd_3;	

#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)	
	u1Byte 	antsel_rx_keep_2:1;	//ex_intf_flg:1;
	u1Byte 	sgi_en:1;
	u1Byte 	rxsc:2;	
	u1Byte 	idle_long:1;
	u1Byte 	r_ant_train_en:1;
	u1Byte 	ant_sel_b:1;
	u1Byte 	ant_sel:1;	
#else	// _BIG_ENDIAN_	
	u1Byte 	ant_sel:1;	
	u1Byte 	ant_sel_b:1;
	u1Byte 	r_ant_train_en:1;
	u1Byte 	idle_long:1;
	u1Byte 	rxsc:2;
	u1Byte 	sgi_en:1;
	u1Byte 	antsel_rx_keep_2:1;	//ex_intf_flg:1;
#endif
} PHY_STATUS_RPT_8192CD_T,*PPHY_STATUS_RPT_8192CD_T;


typedef struct _Phy_Status_Rpt_8812
{
#if 0
	PHY_RX_AGC_INFO_T path_agc[2];
	u1Byte 	ch_num[2];									
	u1Byte	cck_sig_qual_ofdm_pwdb_all;
	u1Byte	cck_agc_rpt_ofdm_cfosho_a;
	u1Byte	cck_bb_pwr_ofdm_cfosho_b;
	u1Byte    cck_rx_path;	//CCK_RX_PATH [3:0] (with regA07[3:0] definition)	
	u1Byte 	rsvd_1;	
	u1Byte	path_cfotail[2];	
	u1Byte	pcts_mask[2];	
	s1Byte	stream_rxevm[2];	
	u1Byte	path_rxsnr[2];
	u1Byte	rsvd_2[2];	
	u1Byte 	stream_snr[2];	
	u1Byte 	stream_csi[2];
	u1Byte	rsvd_3[2];
	s1Byte 	sig_evm;
	u1Byte 	rsvd_4;	
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)	
	u1Byte 	antidx_anta:3;
	u1Byte 	antidx_antb:3;
	u1Byte 	rsvd_5:2;
#else	// _BIG_ENDIAN_	
	u1Byte 	rsvd_5:2;
	u1Byte 	antidx_antb:3;
	u1Byte 	antidx_anta:3;	
#endif
#endif

	//2012.05.24 LukeLee: This structure should take big/little endian in consideration later.....
	
	//DWORD 0
	u1Byte			gain_trsw[2];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)	
	u2Byte			chl_num:10;
	u2Byte			sub_chnl:4;
	u2Byte			r_RFMOD:2;
#else	// _BIG_ENDIAN_	
	u2Byte			r_RFMOD:2;
	u2Byte			sub_chnl:4;
	u2Byte			chl_num:10;
#endif

	//DWORD 1
	u1Byte			pwdb_all;
	u1Byte			cfosho[4];	// DW 1 byte 1 DW 2 byte 0

	//DWORD 2
	s1Byte			cfotail[4];	// DW 2 byte 1 DW 3 byte 0

	//DWORD 3
	s1Byte			rxevm[2];	// DW 3 byte 1 DW 3 byte 2
	s1Byte			rxsnr[2];	// DW 3 byte 3 DW 4 byte 0

	//DWORD 4
	u1Byte			PCTS_MSK_RPT[2];	
	u1Byte			pdsnr[2];	// DW 4 byte 3 DW 5 Byte 0

	//DWORD 5
	u1Byte			csi_current[2];
	u1Byte			rx_gain_c;

	//DWORD 6
	u1Byte			rx_gain_d;
	s1Byte			sigevm;
	u1Byte			resvd_0;
	u1Byte			antidx_anta:3;
	u1Byte			antidx_antb:3;
	u1Byte			resvd_1:2;
} PHY_STATUS_RPT_8812_T,*PPHY_STATUS_RPT_8812_T;


VOID
odm_Init_RSSIForDM(
	IN OUT	PDM_ODM_T	pDM_Odm
	);

VOID
ODM_PhyStatusQuery(
	IN OUT	PDM_ODM_T					pDM_Odm,
	OUT		PODM_PHY_INFO_T			pPhyInfo,
	IN 		pu1Byte						pPhyStatus,	
	IN		PODM_PACKET_INFO_T			pPktinfo
	);

VOID
ODM_MacStatusQuery(
	IN OUT	PDM_ODM_T					pDM_Odm,
	IN 		pu1Byte						pMacStatus,
	IN		u1Byte						MacID,	
	IN		BOOLEAN						bPacketMatchBSSID,
	IN		BOOLEAN						bPacketToSelf,
	IN		BOOLEAN						bPacketBeacon
	);
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE|ODM_AP))

HAL_STATUS
ODM_ConfigRFWithTxPwrTrackHeaderFile(
	IN 	PDM_ODM_T	        	pDM_Odm
    );
    
HAL_STATUS
ODM_ConfigRFWithHeaderFile(
	IN 	PDM_ODM_T	        	pDM_Odm,
	IN 	ODM_RF_Config_Type 		ConfigType,
	IN 	ODM_RF_RADIO_PATH_E 	eRFPath
	);

HAL_STATUS
ODM_ConfigBBWithHeaderFile(
	IN  	PDM_ODM_T	                pDM_Odm,
	IN	ODM_BB_Config_Type		ConfigType
    );

HAL_STATUS
ODM_ConfigMACWithHeaderFile(
	IN  	PDM_ODM_T	pDM_Odm
    );

HAL_STATUS
ODM_ConfigFWWithHeaderFile(
	IN 	PDM_ODM_T			pDM_Odm,
	IN 	ODM_FW_Config_Type 	ConfigType,
	OUT u1Byte				*pFirmware,
	OUT u4Byte				*pSize
	);

u4Byte 
ODM_GetHWImgVersion(
	IN	PDM_ODM_T	pDM_Odm
	);

s4Byte
odm_SignalScaleMapping(	
	IN OUT PDM_ODM_T pDM_Odm,
	IN	s4Byte CurrSig 
	);

#endif

#endif

