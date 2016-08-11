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

__PACK typedef struct _Phy_Rx_AGC_Info
{
	#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)	
		u1Byte	gain:7,trsw:1;			
	#else			
		u1Byte	trsw:1,gain:7;
	#endif
} __WLAN_ATTRIB_PACK__ PHY_RX_AGC_INFO_T, *pPHY_RX_AGC_INFO_T;

__PACK typedef struct _Phy_Status_Rpt_8192cd {
	PHY_RX_AGC_INFO_T path_agc[2];
	u1Byte	ch_corr[2];
	u1Byte	cck_sig_qual_ofdm_pwdb_all;
	u1Byte	cck_agc_rpt_ofdm_cfosho_a;
	u1Byte	cck_rpt_b_ofdm_cfosho_b;
	u1Byte	rsvd_1;/*ch_corr_msb;*/
	u1Byte	noise_power_db_msb;
	s1Byte	path_cfotail[2];
	u1Byte	pcts_mask[2];
	s1Byte	stream_rxevm[2];
	u1Byte	path_rxsnr[2];
	u1Byte	noise_power_db_lsb;
	u1Byte	rsvd_2[3];
	u1Byte	stream_csi[2];
	u1Byte	stream_target_csi[2];
	s1Byte	sig_evm;
	u1Byte	rsvd_3;

#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte	antsel_rx_keep_2: 1;	/*ex_intf_flg:1;*/
	u1Byte	sgi_en: 1;
	u1Byte	rxsc: 2;
	u1Byte	idle_long: 1;
	u1Byte	r_ant_train_en: 1;
	u1Byte	ant_sel_b: 1;
	u1Byte	ant_sel: 1;
#else	/*_BIG_ENDIAN_	*/
	u1Byte	ant_sel: 1;
	u1Byte	ant_sel_b: 1;
	u1Byte	r_ant_train_en: 1;
	u1Byte	idle_long: 1;
	u1Byte	rxsc: 2;
	u1Byte	sgi_en: 1;
	u1Byte	antsel_rx_keep_2: 1;/*ex_intf_flg:1;*/
#endif
} __WLAN_ATTRIB_PACK__ PHY_STATUS_RPT_8192CD_T, *PPHY_STATUS_RPT_8192CD_T;


typedef struct _Phy_Status_Rpt_8812 {
/*	DWORD 0*/
	u1Byte			gain_trsw[2];							/*path-A and path-B {TRSW, gain[6:0] }*/
	u1Byte			chl_num_LSB;							/*channel number[7:0]*/
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte			chl_num_MSB: 2;							/*channel number[9:8]*/
	u1Byte			sub_chnl: 4;								/*sub-channel location[3:0]*/
	u1Byte			r_RFMOD: 2;								/*RF mode[1:0]*/
#else	/*_BIG_ENDIAN_	*/
	u1Byte			r_RFMOD: 2;
	u1Byte			sub_chnl: 4;
	u1Byte			chl_num_MSB: 2;
#endif

/*	DWORD 1*/
	u1Byte			pwdb_all;								/*CCK signal quality / OFDM pwdb all*/
	s1Byte			cfosho[2];		/*DW1 byte 1 DW1 byte2	CCK AGC report and CCK_BB_Power / OFDM Path-A and Path-B short CFO*/
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	/*this should be checked again because the definition of 8812 and 8814 is different*/
/*	u1Byte			r_cck_rx_enable_pathc:2;					cck rx enable pathc[1:0]*/
/*	u1Byte			cck_rx_path:4;							cck rx path[3:0]*/
	u1Byte			resvd_0: 6;
	u1Byte			bt_RF_ch_MSB: 2;						/*8812A:2'b0			8814A: bt rf channel keep[7:6]*/
#else	/*_BIG_ENDIAN_*/
	u1Byte			bt_RF_ch_MSB: 2;
	u1Byte			resvd_0: 6;
#endif

/*	DWORD 2*/
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte			ant_div_sw_a: 1;							/*8812A: ant_div_sw_a    8814A: 1'b0*/
	u1Byte			ant_div_sw_b: 1;							/*8812A: ant_div_sw_b    8814A: 1'b0*/
	u1Byte			bt_RF_ch_LSB: 6;						/*8812A: 6'b0                   8814A: bt rf channel keep[5:0]*/
#else	/*_BIG_ENDIAN_	*/
	u1Byte			bt_RF_ch_LSB: 6;
	u1Byte			ant_div_sw_b: 1;
	u1Byte			ant_div_sw_a: 1;
#endif
	s1Byte			cfotail[2];		   /*DW2 byte 1 DW2 byte 2	path-A and path-B CFO tail*/
	u1Byte			PCTS_MSK_RPT_0;						/*PCTS mask report[7:0]*/
	u1Byte			PCTS_MSK_RPT_1;						/*PCTS mask report[15:8]*/

/*	DWORD 3*/
	s1Byte			rxevm[2];	         /*DW3 byte 1 DW3 byte 2	stream 1 and stream 2 RX EVM*/
	s1Byte			rxsnr[2];	         /*DW3 byte 3 DW4 byte 0	path-A and path-B RX SNR*/

/*	DWORD 4*/
	u1Byte			PCTS_MSK_RPT_2;						/*PCTS mask report[23:16]*/
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte			PCTS_MSK_RPT_3: 6;						/*PCTS mask report[29:24]*/
	u1Byte			pcts_rpt_valid: 1;							/*pcts_rpt_valid*/
	u1Byte			resvd_1: 1;								/*1'b0*/
#else	/*_BIG_ENDIAN_*/
	u1Byte			resvd_1: 1;
	u1Byte			pcts_rpt_valid: 1;
	u1Byte			PCTS_MSK_RPT_3: 6;
#endif
	s1Byte			rxevm_cd[2];	   /*DW 4 byte 3 DW5 byte 0  8812A: 16'b0	8814A: stream 3 and stream 4 RX EVM*/

/*	DWORD 5*/
	u1Byte			csi_current[2];	   /*DW5 byte 1 DW5 byte 2	8812A: stream 1 and 2 CSI	8814A:  path-C and path-D RX SNR*/
	u1Byte			gain_trsw_cd[2];	   /*DW5 byte 3 DW6 byte 0	path-C and path-D {TRSW, gain[6:0] }*/

/*	DWORD 6*/
	s1Byte			sigevm;									/*signal field EVM*/
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte			antidx_antc: 3;							/*8812A: 3'b0		8814A: antidx_antc[2:0]*/
	u1Byte			antidx_antd: 3;							/*8812A: 3'b0		8814A: antidx_antd[2:0]*/
	u1Byte			dpdt_ctrl_keep: 1;						/*8812A: 1'b0		8814A: dpdt_ctrl_keep*/
	u1Byte			GNT_BT_keep: 1;							/*8812A: 1'b0		8814A: GNT_BT_keep*/
#else	/*_BIG_ENDIAN_*/
	u1Byte			GNT_BT_keep: 1;
	u1Byte			dpdt_ctrl_keep: 1;
	u1Byte			antidx_antd: 3;
	u1Byte			antidx_antc: 3;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte			antidx_anta: 3;							/*antidx_anta[2:0]*/
	u1Byte			antidx_antb: 3;							/*antidx_antb[2:0]*/
	u1Byte			hw_antsw_occur: 2;								/*1'b0*/
#else	/*_BIG_ENDIAN_*/
	u1Byte			hw_antsw_occur: 2;
	u1Byte			antidx_antb: 3;
	u1Byte			antidx_anta: 3;
#endif
} PHY_STATUS_RPT_8812_T, *PPHY_STATUS_RPT_8812_T;

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

#if (RTL8822B_SUPPORT == 1)
/*For 8822B only!! need to move to FW finally */
/*==============================================*/
VOID
phydm_RxPhyStatusJaguarSeries2(
	IN		PDM_ODM_T					pPhydm,
	IN		pu1Byte						pPhyStatus,
	IN		PODM_PACKET_INFO_T			pPktinfo,
	OUT		PODM_PHY_INFO_T			pPhyInfo
);

typedef struct _Phy_Status_Rpt_Jaguar2_Type0 {
	/* DW0 */
	u1Byte		page_num;
	u1Byte		pwdb;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		gain: 6;
	u1Byte		rsvd_0: 1;
	u1Byte		trsw: 1;
#else
	u1Byte		trsw: 1;
	u1Byte		rsvd_0: 1;
	u1Byte		gain: 6;
#endif
	u1Byte		rsvd_1;

	/* DW1 */
	u1Byte		rsvd_2;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		rxsc: 4;
	u1Byte		agc_table: 4;
#else
	u1Byte		agc_table: 4;
	u1Byte		rxsc: 4;
#endif
	u1Byte		channel;
	u1Byte		band;

	/* DW2 */
	u2Byte		length;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		antidx_a: 3;
	u1Byte		antidx_b: 3;
	u1Byte		rsvd_3: 2;
	u1Byte		antidx_c: 3;
	u1Byte		antidx_d: 3;
	u1Byte		rsvd_4:2;
#else
	u1Byte		rsvd_3: 2;
	u1Byte		antidx_b: 3;
	u1Byte		antidx_a: 3;
	u1Byte		rsvd_4:2;
	u1Byte		antidx_d: 3;
	u1Byte		antidx_c: 3;
#endif

	/* DW3 */
	u1Byte		signal_quality;
	u1Byte		agc_rpt;
	u1Byte		bb_power;
	u1Byte		rsvd_5;

	/* DW4 */
	u4Byte		rsvd_6;

	/* DW5 */
	u4Byte		rsvd_7;

	/* DW6 */
	u4Byte		rsvd_8;
} PHY_STATUS_RPT_JAGUAR2_TYPE0, *PPHY_STATUS_RPT_JAGUAR2_TYPE0;

typedef struct _Phy_Status_Rpt_Jaguar2_Type1 {
	/* DW0 and DW1 */
	u1Byte		page_num;
	u1Byte		pwdb[4];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		l_rxsc: 4;
	u1Byte		ht_rxsc: 4;
#else
	u1Byte		ht_rxsc: 4;
	u1Byte		l_rxsc: 4;
#endif
	u1Byte		channel;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		band: 2;
	u1Byte		rsvd_0: 1;
	u1Byte		hw_antsw_occu: 1;
	u1Byte		gnt_bt: 1;
	u1Byte		ldpc: 1;
	u1Byte		stbc: 1;
	u1Byte		beamformed: 1;
#else
	u1Byte		beamformed: 1;
	u1Byte		stbc: 1;
	u1Byte		ldpc: 1;
	u1Byte		gnt_bt: 1;
	u1Byte		hw_antsw_occu: 1;
	u1Byte		rsvd_0: 1;
	u1Byte		band: 2;
#endif

	/* DW2 */
	u2Byte		lsig_length;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		antidx_a: 3;
	u1Byte		antidx_b: 3;
	u1Byte		rsvd_1: 2;
	u1Byte		antidx_c: 3;
	u1Byte		antidx_d: 3;
	u1Byte		rsvd_2: 2;
#else
	u1Byte		rsvd_1: 2;
	u1Byte		antidx_b: 3;
	u1Byte		antidx_a: 3;
	u1Byte		rsvd_2: 2;
	u1Byte		antidx_d: 3;
	u1Byte		antidx_c: 3;
#endif

	/* DW3 */
	u1Byte		paid;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		paid_msb: 1;
	u1Byte		gid: 6;
	u1Byte		rsvd_3: 1;
#else
	u1Byte		rsvd_3: 1;
	u1Byte		gid: 6;
	u1Byte		paid_msb: 1;
#endif
	u1Byte		intf_pos;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		intf_pos_msb: 1;
	u1Byte		rsvd_4: 2;
	u1Byte		nb_intf_flag: 1;
	u1Byte		rf_mode: 2;
	u1Byte		rsvd_5: 2;
#else
	u1Byte		rsvd_5: 2;
	u1Byte		rf_mode: 2;
	u1Byte		nb_intf_flag: 1;
	u1Byte		rsvd_4: 2;
	u1Byte		intf_pos_msb: 1;
#endif

	/* DW4 */
	s1Byte		rxevm[4];			/* s(8,1) */

	/* DW5 */
	s1Byte		cfo_tail[4];			/* s(8,7) */

	/* DW6 */
	s1Byte		rxsnr[4];			/* s(8,1) */
} PHY_STATUS_RPT_JAGUAR2_TYPE1, *PPHY_STATUS_RPT_JAGUAR2_TYPE1;

typedef struct _Phy_Status_Rpt_Jaguar2_Type2 {
	/* DW0 ane DW1 */
	u1Byte		page_num;
	u1Byte		pwdb[4];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)	
	u1Byte		l_rxsc: 4;
	u1Byte		ht_rxsc: 4;
#else
	u1Byte		ht_rxsc: 4;
	u1Byte		l_rxsc: 4;
#endif
	u1Byte		channel;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		band: 2;
	u1Byte		rsvd_0: 1;
	u1Byte		hw_antsw_occu: 1;
	u1Byte		gnt_bt: 1;
	u1Byte		ldpc: 1;
	u1Byte		stbc: 1;
	u1Byte		beamformed: 1;
#else
	u1Byte		beamformed: 1;
	u1Byte		stbc: 1;
	u1Byte		ldpc: 1;
	u1Byte		gnt_bt: 1;
	u1Byte		hw_antsw_occu: 1;
	u1Byte		rsvd_0: 1;
	u1Byte		band: 2;
#endif

	/* DW2 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		shift_l_map: 6;
	u1Byte		rsvd_1: 2;
#else
	u1Byte		rsvd_1: 2;
	u1Byte		shift_l_map: 6;
#endif
	u1Byte		cnt_pw2cca;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		agc_table_a: 4;
	u1Byte		agc_table_b: 4;
	u1Byte		agc_table_c: 4;
	u1Byte		agc_table_d: 4;
#else
	u1Byte		agc_table_b: 4;
	u1Byte		agc_table_a: 4;
	u1Byte		agc_table_d: 4;
	u1Byte		agc_table_c: 4;
#endif

	/* DW3 ~ DW6*/
	u1Byte		cnt_cca2agc_rdy;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		gain_a: 6;
	u1Byte		rsvd_2: 1;
	u1Byte		trsw_a: 1;
	u1Byte		gain_b: 6;
	u1Byte		rsvd_3: 1;
	u1Byte		trsw_b: 1;
	u1Byte		gain_c: 6;
	u1Byte		rsvd_4: 1;
	u1Byte		trsw_c: 1;
	u1Byte		gain_d: 6;
	u1Byte		rsvd_5: 1;
	u1Byte		trsw_d: 1;
	u1Byte		aagc_step_a: 2;
	u1Byte		aagc_step_b: 2;
	u1Byte		aagc_step_c: 2;
	u1Byte		aagc_step_d: 2;
#else
	u1Byte		trsw_a: 1;
	u1Byte		rsvd_2: 1;
	u1Byte		gain_a: 6;
	u1Byte		trsw_b: 1;
	u1Byte		rsvd_3: 1;
	u1Byte		gain_b: 6;
	u1Byte		trsw_c: 1;
	u1Byte		rsvd_4: 1;
	u1Byte		gain_c: 6;
	u1Byte		trsw_d: 1;
	u1Byte		rsvd_5: 1;
	u1Byte		gain_d: 6;
	u1Byte		aagc_step_d: 2;
	u1Byte		aagc_step_c: 2;
	u1Byte		aagc_step_b: 2;
	u1Byte		aagc_step_a: 2;
#endif
	u1Byte		ht_aagc_gain[4];
	u1Byte		dagc_gain[4];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u1Byte		counter: 6;
	u1Byte		rsvd_6: 2;
	u1Byte		syn_count: 5;
	u1Byte		rsvd_7:3;
#else
	u1Byte		rsvd_6: 2;
	u1Byte		counter: 6;
	u1Byte		rsvd_7:3;
	u1Byte		syn_count: 5;
#endif
} PHY_STATUS_RPT_JAGUAR2_TYPE2, *PPHY_STATUS_RPT_JAGUAR2_TYPE2;
/*==============================================*/
#endif
#endif

