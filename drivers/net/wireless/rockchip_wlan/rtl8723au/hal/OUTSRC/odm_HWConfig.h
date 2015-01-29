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

//============================================================
// Definition 
//============================================================
//
//-----------------------------------------------------------
// CCK Rates, TxHT = 0
#define DESC92C_RATE1M					0x00
#define DESC92C_RATE2M					0x01
#define DESC92C_RATE5_5M				0x02
#define DESC92C_RATE11M				0x03

// OFDM Rates, TxHT = 0
#define DESC92C_RATE6M					0x04
#define DESC92C_RATE9M					0x05
#define DESC92C_RATE12M				0x06
#define DESC92C_RATE18M				0x07
#define DESC92C_RATE24M				0x08
#define DESC92C_RATE36M				0x09
#define DESC92C_RATE48M				0x0a
#define DESC92C_RATE54M				0x0b

// MCS Rates, TxHT = 1
#define DESC92C_RATEMCS0				0x0c
#define DESC92C_RATEMCS1				0x0d
#define DESC92C_RATEMCS2				0x0e
#define DESC92C_RATEMCS3				0x0f
#define DESC92C_RATEMCS4				0x10
#define DESC92C_RATEMCS5				0x11
#define DESC92C_RATEMCS6				0x12
#define DESC92C_RATEMCS7				0x13
#define DESC92C_RATEMCS8				0x14
#define DESC92C_RATEMCS9				0x15
#define DESC92C_RATEMCS10				0x16
#define DESC92C_RATEMCS11				0x17
#define DESC92C_RATEMCS12				0x18
#define DESC92C_RATEMCS13				0x19
#define DESC92C_RATEMCS14				0x1a
#define DESC92C_RATEMCS15				0x1b
#define DESC92C_RATEMCS15_SG			0x1c
#define DESC92C_RATEMCS32				0x20


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
	u1Byte	path_cfotail[2];	
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


typedef struct _Phy_Status_Rpt_8195
{
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
} PHY_STATUS_RPT_8195_T,*pPHY_STATUS_RPT_8195_T;


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
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE|ODM_AP))
HAL_STATUS
ODM_ConfigRFWithHeaderFile(
	IN	PDM_ODM_T	      pDM_Odm,
	IN 	ODM_RF_RADIO_PATH_E 	Content,
	IN 	ODM_RF_RADIO_PATH_E 	eRFPath
	);

HAL_STATUS
ODM_ConfigBBWithHeaderFile(
	IN  	PDM_ODM_T	                	pDM_Odm,
	IN	ODM_BB_Config_Type		ConfigType
    );

HAL_STATUS
ODM_ConfigMACWithHeaderFile(
	IN  	PDM_ODM_T	pDM_Odm
    );

s4Byte
odm_SignalScaleMapping(	
	IN OUT PDM_ODM_T pDM_Odm,
	IN	s4Byte CurrSig 
	);

#endif


#endif

