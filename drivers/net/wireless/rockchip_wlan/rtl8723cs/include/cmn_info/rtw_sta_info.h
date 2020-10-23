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

 /*This header file is for all driver teams to use the same station info.
If you want to change this file please make sure notify all driver teams maintainers.*/

/*Created by YuChen 20170301*/

#ifndef __INC_RTW_STA_INFO_H
#define __INC_RTW_STA_INFO_H

/*--------------------Define ---------------------------------------*/

#define STA_DM_CTRL_ACTIVE			BIT(0)
#define STA_DM_CTRL_CFO_TRACKING	BIT(1)

#ifdef CONFIG_BEAMFORMING
#define	BEAMFORMING_HT_BEAMFORMER_ENABLE	BIT(0)	/*Declare sta support beamformer*/
#define	BEAMFORMING_HT_BEAMFORMEE_ENABLE	BIT(1)	/*Declare sta support beamformee*/
#define	BEAMFORMING_HT_BEAMFORMER_TEST		BIT(2)	/*Transmiting Beamforming no matter the target supports it or not*/
#define	BEAMFORMING_HT_BEAMFORMER_STEER_NUM		(BIT(4)|BIT(5))		/*Sta Bfer's capability*/
#define	BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP	(BIT(6)|BIT(7))		/*Sta BFee's capability*/

#define	BEAMFORMING_VHT_BEAMFORMER_ENABLE	BIT(0)	/*Declare sta support beamformer*/
#define	BEAMFORMING_VHT_BEAMFORMEE_ENABLE	BIT(1)	/*Declare sta support beamformee*/
#define	BEAMFORMING_VHT_MU_MIMO_AP_ENABLE	BIT(2)	/*Declare sta support MU beamformer*/
#define	BEAMFORMING_VHT_MU_MIMO_STA_ENABLE	BIT(3)	/*Declare sta support MU beamformer*/
#define	BEAMFORMING_VHT_BEAMFORMER_TEST		BIT(4)	/*Transmiting Beamforming no matter the target supports it or not*/
#define	BEAMFORMING_VHT_BEAMFORMER_STS_CAP		(BIT(8)|BIT(9)|BIT(10))		/*Sta BFee's capability*/
#define	BEAMFORMING_VHT_BEAMFORMEE_SOUND_DIM	(BIT(12)|BIT(13)|BIT(14))	/*Sta Bfer's capability*/
#endif

#define HT_STBC_EN	BIT(0)
#define VHT_STBC_EN	BIT(1)

#define HT_LDPC_EN	BIT(0)
#define VHT_LDPC_EN	BIT(1)

#define	SM_PS_STATIC	0
#define	SM_PS_DYNAMIC	1
#define	SM_PS_INVALID	2
#define	SM_PS_DISABLE	3


/*cmn_sta_info.ra_sta_info.txrx_state*/
#define	TX_STATE				0
#define	RX_STATE				1
#define	BI_DIRECTION_STATE	2

/*--------------------Define Enum-----------------------------------*/
enum channel_width {
	CHANNEL_WIDTH_20		= 0,
	CHANNEL_WIDTH_40		= 1,
	CHANNEL_WIDTH_80		= 2,
	CHANNEL_WIDTH_160		= 3,
	CHANNEL_WIDTH_80_80	= 4,
	CHANNEL_WIDTH_5		= 5,
	CHANNEL_WIDTH_10	= 6,
	CHANNEL_WIDTH_MAX	= 7,
};

enum rf_type {
	RF_1T1R			= 0,
	RF_1T2R			= 1,
	RF_2T2R			= 2,
	RF_2T3R			= 3,
	RF_2T4R			= 4,
	RF_3T3R			= 5,
	RF_3T4R			= 6,
	RF_4T4R			= 7,
	RF_4T3R			= 8,
	RF_4T2R			= 9,
	RF_4T1R			= 10,
	RF_3T2R			= 11,
	RF_3T1R			= 12,
	RF_2T1R			= 13,
	RF_1T4R			= 14,
	RF_1T3R			= 15,
	RF_TYPE_MAX,
};

enum bb_path {
	BB_PATH_NON = 0,
	BB_PATH_A = 0x00000001,
	BB_PATH_B = 0x00000002,
	BB_PATH_C = 0x00000004,
	BB_PATH_D = 0x00000008,

	BB_PATH_AB = (BB_PATH_A | BB_PATH_B),
	BB_PATH_AC = (BB_PATH_A | BB_PATH_C),
	BB_PATH_AD = (BB_PATH_A | BB_PATH_D),
	BB_PATH_BC = (BB_PATH_B | BB_PATH_C),
	BB_PATH_BD = (BB_PATH_B | BB_PATH_D),
	BB_PATH_CD = (BB_PATH_C | BB_PATH_D),

	BB_PATH_ABC = (BB_PATH_A | BB_PATH_B | BB_PATH_C),
	BB_PATH_ABD = (BB_PATH_A | BB_PATH_B | BB_PATH_D),
	BB_PATH_ACD = (BB_PATH_A | BB_PATH_C | BB_PATH_D),
	BB_PATH_BCD = (BB_PATH_B | BB_PATH_C | BB_PATH_D),

	BB_PATH_ABCD = (BB_PATH_A | BB_PATH_B | BB_PATH_C | BB_PATH_D),
	BB_PATH_AUTO = 0xff /*for path diversity*/
};

enum rf_path {
	RF_PATH_A = 0,
	RF_PATH_B = 1,
	RF_PATH_C = 2,
	RF_PATH_D = 3,
	RF_PATH_AB,
	RF_PATH_AC,
	RF_PATH_AD,
	RF_PATH_BC,
	RF_PATH_BD,
	RF_PATH_CD,
	RF_PATH_ABC,
	RF_PATH_ABD,
	RF_PATH_ACD,
	RF_PATH_BCD,
	RF_PATH_ABCD,
};

enum rf_syn {
	RF_SYN0 = 0,
	RF_SYN1 = 1,
};

enum rfc_mode {
	rfc_4x4 = 0,
	rfc_2x2 = 1,
};

enum wireless_set {
	WIRELESS_CCK	= 0x00000001,
	WIRELESS_OFDM	= 0x00000002,
	WIRELESS_HT	= 0x00000004,
	WIRELESS_VHT	= 0x00000008,
};

/*--------------------Define MACRO---------------------------------*/

/*--------------------Define Struct-----------------------------------*/

#ifdef CONFIG_BEAMFORMING
struct bf_cmn_info {
	u8	ht_beamform_cap;		/*Sta capablity*/
	u16	vht_beamform_cap;		/*Sta capablity*/
	u16	p_aid;
	u8	g_id;
};
#endif
struct rssi_info {
	s8		rssi;
	s8		rssi_cck;
	s8		rssi_ofdm;
	u8		packet_map;
	u8		ofdm_pkt_cnt;
	u8		cck_pkt_cnt;
	u16		cck_sum_power;
	u8		is_send_rssi;
	u8		valid_bit;
	s16		rssi_acc;	/*accumulate RSSI for per packet MA sum*/
};

struct ra_sta_info {
	u8	rate_id;			/*[PHYDM] ratr_idx*/
	u8	rssi_level;			/*[PHYDM]*/
	u8	is_first_connect:1;		/*[PHYDM] CE: ra_rpt_linked, AP: H2C_rssi_rpt*/
	u8	is_support_sgi:1;		/*[driver]*/
	u8	is_vht_enable:2;		/*[driver]*/
	u8	disable_ra:1;			/*[driver]*/
	u8	disable_pt:1;			/*[driver] remove is_disable_power_training*/
	u8	txrx_state:2;			/*[PHYDM] 0: Tx, 1:Rx, 2:bi-direction*/
	u8	is_noisy:1;			/*[PHYDM]*/
	u8	curr_tx_rate;			/*[PHYDM] FW->Driver*/
	enum channel_width	ra_bw_mode;	/*[Driver] max bandwidth, for RA only*/
	enum channel_width	curr_tx_bw;	/*[PHYDM] FW->Driver*/
	u8	curr_retry_ratio;		/*[PHYDM] FW->Driver*/
	u64	ramask;
};

struct dtp_info {
	u8	dyn_tx_power;	/*Dynamic Tx power offset*/
	u8	last_tx_power;
	boolean	sta_is_alive;
	u8	sta_tx_high_power_lvl:4;
	u8	sta_last_dtp_lvl:4;
};

struct cmn_sta_info {
	u16	dm_ctrl;			/*[Driver]*/
	enum channel_width	bw_mode;	/*[Driver] max support BW*/
	u8	mac_id;				/*[Driver]*/
	u8	mac_addr[6];			/*[Driver]*/
	u16	aid;				/*[Driver]*/
	enum rf_type mimo_type;			/*[Driver] sta XTXR*/
	struct rssi_info	rssi_stat;	/*[PHYDM]*/
	struct ra_sta_info	ra_info;	/*[Driver&PHYDM]*/
	u16	tx_moving_average_tp;		/*[Driver] tx average MBps*/
	u16	rx_moving_average_tp;		/*[Driver] rx average MBps*/
	u8	stbc_en:2;			/*[Driver] really transmitt STBC*/
	u8	ldpc_en:2;			/*[Driver] really transmitt LDPC*/
	enum wireless_set	support_wireless_set;/*[Driver]*/
#ifdef CONFIG_BEAMFORMING
	struct bf_cmn_info	bf_info;	/*[Driver]*/
#endif
	u8	sm_ps:2;			/*[Driver]*/
	struct dtp_info dtp_stat;		/*[PHYDM] Dynamic Tx power offset*/
	/*u8		pw2cca_over_TH_cnt;*/
	/*u8		total_pw2cca_cnt;*/
};

struct phydm_phyinfo_fw_struct {
	u8		rx_rssi[4];	/* RSSI in 0~100 index */
};

struct phydm_phyinfo_struct {
	boolean		physts_rpt_valid; /* @if physts_rpt_valid is false, please ignore the parsing result in this structure*/
	u8		rx_pwdb_all;
	u8		signal_quality;				/* OFDM: signal_quality=rx_mimo_signal_quality[0], CCK: signal qualityin 0-100 index. */
	u8		rx_mimo_signal_strength[4];	/* RSSI in 0~100 index */
	s8		rx_mimo_signal_quality[4];		/* OFDM: per-path's EVM translate to 0~100% , no used for CCK*/
	u8		rx_mimo_evm_dbm[4];			/* per-path's original EVM (dbm) */
	s16		cfo_short[4];					/* per-path's cfo_short */
	s16		cfo_tail[4];					/* per-path's cfo_tail */
	s8		rx_power;					/* in dBm Translate from PWdB */
	s8		recv_signal_power;			/* Real power in dBm for this packet, no beautification and aggregation. Keep this raw info to be used for the other procedures. */
	u8		bt_rx_rssi_percentage;
	u8		signal_strength;				/* in 0-100 index. */
	s8		rx_pwr[4];					/* per-path's pwdb */
	s8		rx_snr[4];					/* per-path's SNR	*/
	u8		ant_idx[4];	/*per-path's antenna index*/
/*ODM_PHY_STATUS_NEW_TYPE_SUPPORT*/
	u8		rx_count:2;					/* RX path counter---*/
	u8		band_width:3;
	u8		rxsc:4;						/* sub-channel---*/
	u8		channel;						/* channel number---*/
	u8		is_mu_packet:1;				/* is MU packet or not---boolean*/
	u8		is_beamformed:1;				/* BF packet---boolean*/
	u8		cnt_pw2cca;
	u8		cnt_cca2agc_rdy;
/*ODM_PHY_STATUS_NEW_TYPE_SUPPORT*/
	u8		rx_cck_evm;
};

struct phydm_perpkt_info_struct {
	u8		data_rate;
	u8		station_id;
	u8		is_cck_rate: 1;
	u8		rate_ss:3;			/*spatial stream of data rate*/
	u8		is_packet_match_bssid:1;	/*boolean*/
	u8		is_packet_to_self:1;		/*boolean*/
	u8		is_packet_beacon:1;		/*boolean*/
	u8		is_to_self:1;				/*boolean*/
	u8		ppdu_cnt;
};

/*--------------------Export global variable----------------------------*/

/*--------------------Function declaration-----------------------------*/

#endif
