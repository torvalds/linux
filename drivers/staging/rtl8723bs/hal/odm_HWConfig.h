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

struct phy_rx_agc_info_t {
	#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
		u8 gain:7, trsw:1;
	#else
		u8 trsw:1, gain:7;
	#endif
};

struct phy_status_rpt_8192cd_t {
	struct phy_rx_agc_info_t path_agc[2];
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
};

void ODM_PhyStatusQuery(
	struct dm_odm_t *pDM_Odm,
	struct odm_phy_info *pPhyInfo,
	u8 *pPhyStatus,
	struct odm_packet_info *pPktinfo
);

enum hal_status ODM_ConfigRFWithTxPwrTrackHeaderFile(struct dm_odm_t *pDM_Odm);

enum hal_status ODM_ConfigRFWithHeaderFile(
	struct dm_odm_t *pDM_Odm,
	enum ODM_RF_Config_Type ConfigType,
	enum odm_rf_radio_path_e eRFPath
);

enum hal_status ODM_ConfigBBWithHeaderFile(
	struct dm_odm_t *pDM_Odm, enum ODM_BB_Config_Type ConfigType
);

enum hal_status ODM_ConfigFWWithHeaderFile(
	struct dm_odm_t *pDM_Odm,
	enum ODM_FW_Config_Type ConfigType,
	u8 *pFirmware,
	u32 *pSize
);

s32 odm_SignalScaleMapping(struct dm_odm_t *pDM_Odm, s32 CurrSig);

#endif
