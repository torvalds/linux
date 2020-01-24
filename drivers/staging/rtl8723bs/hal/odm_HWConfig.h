/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/


#ifndef __HALHWOUTSRC_H__
#define __HALHWOUTSRC_H__


/*--------------------------Define -------------------------------------------*/
/* define READ_NEXT_PAIR(v1, v2, i) do { i += 2; v1 = Array[i]; v2 = Array[i+1]; } while (0) */
#define AGC_DIFF_CONFIG_MP(ic, band) (ODM_ReadAndConfig_MP_##ic##_AGC_TAB_DIFF(pDM_Odm, Array_MP_##ic##_AGC_TAB_DIFF_##band, \
	sizeof(Array_MP_##ic##_AGC_TAB_DIFF_##band)/sizeof(u32)))
#define AGC_DIFF_CONFIG_TC(ic, band) (ODM_ReadAndConfig_TC_##ic##_AGC_TAB_DIFF(pDM_Odm, Array_TC_##ic##_AGC_TAB_DIFF_##band, \
	sizeof(Array_TC_##ic##_AGC_TAB_DIFF_##band)/sizeof(u32)))

#define AGC_DIFF_CONFIG(ic, band)\
	do {\
		if (pDM_Odm->bIsMPChip)\
			AGC_DIFF_CONFIG_MP(ic, band);\
		else\
			AGC_DIFF_CONFIG_TC(ic, band);\
	} while (0)


/*  */
/*  structure and define */
/*  */

typedef struct _Phy_Rx_AGC_Info {
	#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
		u8 gain:7, trsw:1;
	#else
		u8 trsw:1, gain:7;
	#endif
} PHY_RX_AGC_INFO_T, *pPHY_RX_AGC_INFO_T;

typedef struct _Phy_Status_Rpt_8192cd {
	PHY_RX_AGC_INFO_T path_agc[2];
	u8 ch_corr[2];
	u8 cck_sig_qual_ofdm_pwdb_all;
	u8 cck_agc_rpt_ofdm_cfosho_a;
	u8 cck_rpt_b_ofdm_cfosho_b;
	u8 rsvd_1;/* ch_corr_msb; */
	u8 noise_power_db_msb;
	s8 path_cfotail[2];
	u8 pcts_mask[2];
	s8 stream_rxevm[2];
	u8 path_rxsnr[2];
	u8 noise_power_db_lsb;
	u8 rsvd_2[3];
	u8 stream_csi[2];
	u8 stream_target_csi[2];
	s8	sig_evm;
	u8 rsvd_3;

#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antsel_rx_keep_2:1;	/* ex_intf_flg:1; */
	u8 sgi_en:1;
	u8 rxsc:2;
	u8 idle_long:1;
	u8 r_ant_train_en:1;
	u8 ant_sel_b:1;
	u8 ant_sel:1;
#else	/*  _BIG_ENDIAN_ */
	u8 ant_sel:1;
	u8 ant_sel_b:1;
	u8 r_ant_train_en:1;
	u8 idle_long:1;
	u8 rxsc:2;
	u8 sgi_en:1;
	u8 antsel_rx_keep_2:1;	/* ex_intf_flg:1; */
#endif
} PHY_STATUS_RPT_8192CD_T, *PPHY_STATUS_RPT_8192CD_T;


typedef struct _Phy_Status_Rpt_8812 {
	/* 2012.05.24 LukeLee: This structure should take big/little endian in consideration later..... */

	/* DWORD 0 */
	u8 gain_trsw[2];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u16 chl_num:10;
	u16 sub_chnl:4;
	u16 r_RFMOD:2;
#else	/*  _BIG_ENDIAN_ */
	u16 r_RFMOD:2;
	u16 sub_chnl:4;
	u16 chl_num:10;
#endif

	/* DWORD 1 */
	u8 pwdb_all;
	u8 cfosho[4];	/*  DW 1 byte 1 DW 2 byte 0 */

	/* DWORD 2 */
	s8 cfotail[4]; /*  DW 2 byte 1 DW 3 byte 0 */

	/* DWORD 3 */
	s8 rxevm[2]; /*  DW 3 byte 1 DW 3 byte 2 */
	s8 rxsnr[2]; /*  DW 3 byte 3 DW 4 byte 0 */

	/* DWORD 4 */
	u8 PCTS_MSK_RPT[2];
	u8 pdsnr[2]; /*  DW 4 byte 3 DW 5 Byte 0 */

	/* DWORD 5 */
	u8 csi_current[2];
	u8 rx_gain_c;

	/* DWORD 6 */
	u8 rx_gain_d;
	s8 sigevm;
	u8 resvd_0;
	u8 antidx_anta:3;
	u8 antidx_antb:3;
	u8 resvd_1:2;
} PHY_STATUS_RPT_8812_T, *PPHY_STATUS_RPT_8812_T;


void ODM_PhyStatusQuery(
	PDM_ODM_T pDM_Odm,
	struct odm_phy_info *pPhyInfo,
	u8 *pPhyStatus,
	struct odm_packet_info *pPktinfo
);

HAL_STATUS ODM_ConfigRFWithTxPwrTrackHeaderFile(PDM_ODM_T pDM_Odm);

HAL_STATUS ODM_ConfigRFWithHeaderFile(
	PDM_ODM_T pDM_Odm,
	ODM_RF_Config_Type ConfigType,
	ODM_RF_RADIO_PATH_E eRFPath
);

HAL_STATUS ODM_ConfigBBWithHeaderFile(
	PDM_ODM_T pDM_Odm, ODM_BB_Config_Type ConfigType
);

HAL_STATUS ODM_ConfigFWWithHeaderFile(
	PDM_ODM_T pDM_Odm,
	ODM_FW_Config_Type ConfigType,
	u8 *pFirmware,
	u32 *pSize
);

s32 odm_SignalScaleMapping(PDM_ODM_T pDM_Odm, s32 CurrSig);

#endif
