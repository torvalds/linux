/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __PHYDM_PHYSTATUS_H__
#define __PHYDM_PHYSTATUS_H__

/*@--------------------------Define ------------------------------------------*/
#define CCK_RSSI_INIT_COUNT 5

#define RA_RSSI_STATE_INIT 0
#define RA_RSSI_STATE_SEND 1
#define RA_RSSI_STATE_HOLD 2

#if defined(DM_ODM_CE_MAC80211)
#define CFO_HW_RPT_2_KHZ(val) ({		\
	s32 cfo_hw_rpt_2_khz_tmp = (val);	\
	(cfo_hw_rpt_2_khz_tmp << 1) + (cfo_hw_rpt_2_khz_tmp >> 1);	\
	})
#else
#define CFO_HW_RPT_2_KHZ(val) ((val << 1) + (val >> 1))
#endif

/* @(X* 312.5 Khz)>>7 ~=  X*2.5 Khz= (X<<1 + X>>1)Khz  */

#define IGI_2_RSSI(igi) (igi - 10)

#define PHY_STATUS_JRGUAR2_DW_LEN 7 /* @7*4 = 28 Byte */
#define PHY_STATUS_JRGUAR3_DW_LEN 7 /* @7*4 = 28 Byte */
#define SHOW_PHY_STATUS_UNLIMITED 0
#define RSSI_MA 4 /*moving average factor for RSSI: 2^4=16 */

#define PHYSTS_PATH_NUM 4

/*@************************************************************
 * structure and define
 ************************************************************/

__PACK struct phy_rx_agc_info {
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 gain : 7, trsw : 1;
#else
	u8 trsw : 1, gain : 7;
#endif
};

__PACK struct phy_status_rpt_8192cd {
	struct phy_rx_agc_info path_agc[2];
	u8	ch_corr[2];
	u8	cck_sig_qual_ofdm_pwdb_all;
	u8	cck_agc_rpt_ofdm_cfosho_a;
	u8	cck_rpt_b_ofdm_cfosho_b;
	u8	rsvd_1;/*@ch_corr_msb;*/
	u8	noise_power_db_msb;
	s8	path_cfotail[2];
	u8	pcts_mask[2];
	s8	stream_rxevm[2];
	u8	path_rxsnr[2];
	u8	noise_power_db_lsb;
	u8	rsvd_2[3];
	u8	stream_csi[2];
	u8	stream_target_csi[2];
	s8	sig_evm;
	u8	rsvd_3;

#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8	antsel_rx_keep_2: 1;	/*@ex_intf_flg:1;*/
	u8	sgi_en: 1;
	u8	rxsc: 2;
	u8	idle_long: 1;
	u8	r_ant_train_en: 1;
	u8	ant_sel_b: 1;
	u8	ant_sel: 1;
#else	/*@_BIG_ENDIAN_	*/
	u8	ant_sel: 1;
	u8	ant_sel_b: 1;
	u8	r_ant_train_en: 1;
	u8	idle_long: 1;
	u8	rxsc: 2;
	u8	sgi_en: 1;
	u8	antsel_rx_keep_2: 1;/*@ex_intf_flg:1;*/
#endif
};

struct phy_status_rpt_8812 {
	/*	@DWORD 0*/
	u8 gain_trsw[2]; /*path-A and path-B {TRSW, gain[6:0] }*/
	u8 chl_num_LSB; /*@channel number[7:0]*/
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 chl_num_MSB : 2; /*@channel number[9:8]*/
	u8 sub_chnl : 4; /*sub-channel location[3:0]*/
	u8 r_RFMOD : 2; /*RF mode[1:0]*/
#else /*@_BIG_ENDIAN_	*/
	u8 r_RFMOD : 2;
	u8 sub_chnl : 4;
	u8 chl_num_MSB : 2;
#endif

	/*	@DWORD 1*/
	u8 pwdb_all; /*@CCK signal quality / OFDM pwdb all*/
	s8 cfosho[2]; /*@CCK AGC report and CCK_BB_Power*/
		      /*OFDM path-A and path-B short CFO*/
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 resvd_0 : 6;
	u8 bt_RF_ch_MSB : 2; /*@8812A:2'b0  8814A: bt rf channel keep[7:6]*/
#else /*@_BIG_ENDIAN_*/
	u8 bt_RF_ch_MSB : 2;
	u8 resvd_0 : 6;
#endif

/*	@DWORD 2*/
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 ant_div_sw_a : 1; /*@8812A: ant_div_sw_a    8814A: 1'b0*/
	u8 ant_div_sw_b : 1; /*@8812A: ant_div_sw_b    8814A: 1'b0*/
	u8 bt_RF_ch_LSB : 6; /*@8812A: 6'b0     8814A: bt rf channel keep[5:0]*/
#else /*@_BIG_ENDIAN_	*/
	u8 bt_RF_ch_LSB : 6;
	u8 ant_div_sw_b : 1;
	u8 ant_div_sw_a : 1;
#endif
	s8 cfotail[2]; /*@DW2 byte 1 DW2 byte 2	path-A and path-B CFO tail*/
	u8 PCTS_MSK_RPT_0; /*PCTS mask report[7:0]*/
	u8 PCTS_MSK_RPT_1; /*PCTS mask report[15:8]*/

	/*	@DWORD 3*/
	s8 rxevm[2]; /*@DW3 byte 1 DW3 byte 2	stream 1 and stream 2 RX EVM*/
	s8 rxsnr[2]; /*@DW3 byte 3 DW4 byte 0	path-A and path-B RX SNR*/

	/*	@DWORD 4*/
	u8 PCTS_MSK_RPT_2; /*PCTS mask report[23:16]*/
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 PCTS_MSK_RPT_3 : 6; /*PCTS mask report[29:24]*/
	u8 pcts_rpt_valid : 1; /*pcts_rpt_valid*/
	u8 resvd_1 : 1; /*@1'b0*/
#else /*@_BIG_ENDIAN_*/
	u8 resvd_1 : 1;
	u8 pcts_rpt_valid : 1;
	u8 PCTS_MSK_RPT_3 : 6;
#endif
	s8 rxevm_cd[2]; /*@8812A: 16'b0*/
			/*@8814A: stream 3 and stream 4 RX EVM*/
	/*	@DWORD 5*/
	u8 csi_current[2]; /*@8812A: stream 1 and 2 CSI*/
			   /*@8814A:  path-C and path-D RX SNR*/
	u8 gain_trsw_cd[2]; /*path-C and path-D {TRSW, gain[6:0] }*/

	/*	@DWORD 6*/
	s8 sigevm; /*signal field EVM*/
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_antc : 3;	/*@8812A: 3'b0	8814A: antidx_antc[2:0]*/
	u8 antidx_antd : 3;	/*@8812A: 3'b0	8814A: antidx_antd[2:0]*/
	u8 dpdt_ctrl_keep : 1;	/*@8812A: 1'b0	8814A: dpdt_ctrl_keep*/
	u8 GNT_BT_keep : 1;	/*@8812A: 1'b0	8814A: GNT_BT_keep*/
#else /*@_BIG_ENDIAN_*/
	u8 GNT_BT_keep : 1;
	u8 dpdt_ctrl_keep : 1;
	u8 antidx_antd : 3;
	u8 antidx_antc : 3;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_anta : 3; /*@antidx_anta[2:0]*/
	u8 antidx_antb : 3; /*@antidx_antb[2:0]*/
	u8 hw_antsw_occur : 2; /*@1'b0*/
#else /*@_BIG_ENDIAN_*/
	u8 hw_antsw_occur : 2;
	u8 antidx_antb : 3;
	u8 antidx_anta : 3;
#endif
};

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)

__PACK struct phy_sts_rpt_jgr2_type0 {
	/* @DW0 */
	u8 page_num;
	u8 pwdb;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 gain : 6;
	u8 rsvd_0 : 1;
	u8 trsw : 1;
#else
	u8 trsw : 1;
	u8 rsvd_0 : 1;
	u8 gain : 6;
#endif
	u8 rsvd_1;

	/* @DW1 */
	u8 rsvd_2;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 rxsc : 4;
	u8 agc_table : 4;
#else
	u8 agc_table : 4;
	u8 rxsc : 4;
#endif
	u8 channel;
	u8 band;

	/* @DW2 */
	u16 length;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_a : 3;
	u8 antidx_b : 3;
	u8 rsvd_3 : 2;
	u8 antidx_c : 3;
	u8 antidx_d : 3;
	u8 rsvd_4 : 2;
#else
	u8 rsvd_3 : 2;
	u8 antidx_b : 3;
	u8 antidx_a : 3;
	u8 rsvd_4 : 2;
	u8 antidx_d : 3;
	u8 antidx_c : 3;
#endif

	/* @DW3 */
	u8 signal_quality;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 vga : 5;
	u8 lna_l : 3;
	u8 bb_power : 6;
	u8 rsvd_9 : 1;
	u8 lna_h : 1;
#else
	u8 lna_l : 3;
	u8 vga : 5;
	u8 lna_h : 1;
	u8 rsvd_9 : 1;
	u8 bb_power : 6;
#endif
	u8 rsvd_5;

	/* @DW4 */
	u32 rsvd_6;

	/* @DW5 */
	u32 rsvd_7;

	/* @DW6 */
	u32 rsvd_8;
};

__PACK struct phy_sts_rpt_jgr2_type1 {
	/* @DW0 and DW1 */
	u8 page_num;
	u8 pwdb[4];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 l_rxsc : 4;
	u8 ht_rxsc : 4;
#else
	u8 ht_rxsc : 4;
	u8 l_rxsc : 4;
#endif
	u8 channel;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 band : 2;
	u8 rsvd_0 : 1;
	u8 hw_antsw_occu : 1;
	u8 gnt_bt : 1;
	u8 ldpc : 1;
	u8 stbc : 1;
	u8 beamformed : 1;
#else
	u8 beamformed : 1;
	u8 stbc : 1;
	u8 ldpc : 1;
	u8 gnt_bt : 1;
	u8 hw_antsw_occu : 1;
	u8 rsvd_0 : 1;
	u8 band : 2;
#endif

	/* @DW2 */
	u16 lsig_length;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_a : 3;
	u8 antidx_b : 3;
	u8 rsvd_1 : 2;
	u8 antidx_c : 3;
	u8 antidx_d : 3;
	u8 rsvd_2 : 2;
#else
	u8 rsvd_1 : 2;
	u8 antidx_b : 3;
	u8 antidx_a : 3;
	u8 rsvd_2 : 2;
	u8 antidx_d : 3;
	u8 antidx_c : 3;
#endif

	/* @DW3 */
	u8 paid;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 paid_msb : 1;
	u8 gid : 6;
	u8 rsvd_3 : 1;
#else
	u8 rsvd_3 : 1;
	u8 gid : 6;
	u8 paid_msb : 1;
#endif
	u8 intf_pos;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 intf_pos_msb : 1;
	u8 rsvd_4 : 2;
	u8 nb_intf_flag : 1;
	u8 rf_mode : 2;
	u8 rsvd_5 : 2;
#else
	u8 rsvd_5 : 2;
	u8 rf_mode : 2;
	u8 nb_intf_flag : 1;
	u8 rsvd_4 : 2;
	u8 intf_pos_msb : 1;
#endif

	/* @DW4 */
	s8 rxevm[4]; /* s(8,1) */

	/* @DW5 */
	s8 cfo_tail[4]; /* s(8,7) */

	/* @DW6 */
	s8 rxsnr[4]; /* s(8,1) */
};

__PACK struct phy_sts_rpt_jgr2_type2 {
	/* @DW0 ane DW1 */
	u8 page_num;
	u8 pwdb[4];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 l_rxsc : 4;
	u8 ht_rxsc : 4;
#else
	u8 ht_rxsc : 4;
	u8 l_rxsc : 4;
#endif
	u8 channel;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 band : 2;
	u8 rsvd_0 : 1;
	u8 hw_antsw_occu : 1;
	u8 gnt_bt : 1;
	u8 ldpc : 1;
	u8 stbc : 1;
	u8 beamformed : 1;
#else
	u8 beamformed : 1;
	u8 stbc : 1;
	u8 ldpc : 1;
	u8 gnt_bt : 1;
	u8 hw_antsw_occu : 1;
	u8 rsvd_0 : 1;
	u8 band : 2;
#endif

/* @DW2 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 shift_l_map : 6;
	u8 rsvd_1 : 2;
#else
	u8 rsvd_1 : 2;
	u8 shift_l_map : 6;
#endif
	u8 cnt_pw2cca;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 agc_table_a : 4;
	u8 agc_table_b : 4;
	u8 agc_table_c : 4;
	u8 agc_table_d : 4;
#else
	u8 agc_table_b : 4;
	u8 agc_table_a : 4;
	u8 agc_table_d : 4;
	u8 agc_table_c : 4;
#endif

	/* @DW3 ~ DW6*/
	u8 cnt_cca2agc_rdy;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 gain_a : 6;
	u8 rsvd_2 : 1;
	u8 trsw_a : 1;
	u8 gain_b : 6;
	u8 rsvd_3 : 1;
	u8 trsw_b : 1;
	u8 gain_c : 6;
	u8 rsvd_4 : 1;
	u8 trsw_c : 1;
	u8 gain_d : 6;
	u8 rsvd_5 : 1;
	u8 trsw_d : 1;
	u8 aagc_step_a : 2;
	u8 aagc_step_b : 2;
	u8 aagc_step_c : 2;
	u8 aagc_step_d : 2;
#else
	u8 trsw_a : 1;
	u8 rsvd_2 : 1;
	u8 gain_a : 6;
	u8 trsw_b : 1;
	u8 rsvd_3 : 1;
	u8 gain_b : 6;
	u8 trsw_c : 1;
	u8 rsvd_4 : 1;
	u8 gain_c : 6;
	u8 trsw_d : 1;
	u8 rsvd_5 : 1;
	u8 gain_d : 6;
	u8 aagc_step_d : 2;
	u8 aagc_step_c : 2;
	u8 aagc_step_b : 2;
	u8 aagc_step_a : 2;
#endif
	u8 ht_aagc_gain[4];
	u8 dagc_gain[4];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 counter : 6;
	u8 rsvd_6 : 2;
	u8 syn_count : 5;
	u8 rsvd_7 : 3;
#else
	u8 rsvd_6 : 2;
	u8 counter : 6;
	u8 rsvd_7 : 3;
	u8 syn_count : 5;
#endif
};
#endif

/*@==============================================*/
#ifdef PHYSTS_3RD_TYPE_SUPPORT
__PACK struct phy_sts_rpt_jgr3_type0 {
/* @DW0 : Offset 0 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 page_num : 4;
	u8 pkt_cnt : 2;
	u8 channel_msb : 2;
#else
	u8 channel_msb : 2;
	u8 pkt_cnt : 2;
	u8 page_num : 4;
#endif
	u8 pwdb_a;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 gain_a : 6;
	u8 rsvd_0 : 1;
	u8 trsw : 1;
#else
	u8 trsw : 1;
	u8 rsvd_0 : 1;
	u8 gain_a : 6;
#endif

#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 agc_table_b : 4;
	u8 agc_table_c : 4;
#else
	u8 agc_table_c : 4;
	u8 agc_table_b : 4;
#endif

/* @DW1 : Offset 4 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 rsvd_1 : 4;
	u8 agc_table_d : 4;
#else
	u8 agc_table_d : 4;
	u8 rsvd_1 : 4;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 l_rxsc : 4;
	u8 agc_table_a : 4;
#else
	u8 agc_table_a : 4;
	u8 l_rxsc : 4;
#endif
	u8 channel;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 band : 2;
	u8 rsvd_2_1 : 1;
	u8 hw_antsw_occur_keep_cck : 1;
	u8 gnt_bt_keep_cck : 1;
	u8 rsvd_2_2 : 1;
	u8 path_sel_o : 2;
#else
	u8 path_sel_o : 2;
	u8 rsvd_2_2 : 1;
	u8 gnt_bt_keep_cck : 1;
	u8 hw_antsw_occur_keep_cck : 1;
	u8 rsvd_2_1 : 1;
	u8 band : 2;
#endif

	/* @DW2 : Offset 8 */
	u16 length;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_a : 4;
	u8 antidx_b : 4;
#else
	u8 antidx_b : 4;
	u8 antidx_a : 4;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_c : 4;
	u8 antidx_d : 4;
#else
	u8 antidx_d : 4;
	u8 antidx_c : 4;
#endif

	/* @DW3 : Offset 12 */
	u8 signal_quality;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 vga_a : 5;
	u8 lna_l_a : 3;
#else
	u8 lna_l_a : 3;
	u8 vga_a : 5;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 bb_power_a : 6;
	u8 rsvd_3_1 : 1;
	u8 lna_h_a : 1;
#else

	u8 lna_h_a : 1;
	u8 rsvd_3_1 : 1;
	u8 bb_power_a : 6;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 rxrate : 2;
	u8 raterr : 1;
	u8 lockbit : 1;
	u8 sqloss : 1;
	u8 mf_off : 1;
	u8 rsvd_3_2 : 2;
#else
	u8 rsvd_3_2 : 2;
	u8 mf_off : 1;
	u8 sqloss : 1;
	u8 lockbit : 1;
	u8 raterr : 1;
	u8 rxrate : 2;
#endif

	/* @DW4 : Offset 16 */
	u8 pwdb_b;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 vga_b : 5;
	u8 lna_l_b : 3;
#else
	u8 lna_l_b : 3;
	u8 vga_b : 5;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 bb_power_b : 6;
	u8 rsvd_4_1 : 1;
	u8 lna_h_b : 1;
#else
	u8 lna_h_b : 1;
	u8 rsvd_4_1 : 1;
	u8 bb_power_b : 6;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 gain_b : 6;
	u8 rsvd_4_2 : 2;
#else
	u8 rsvd_4_2 : 2;
	u8 gain_b : 6;
#endif

	/* @DW5 : Offset 20 */
	u8 pwdb_c;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 vga_c : 5;
	u8 lna_l_c : 3;
#else
	u8 lna_l_c : 3;
	u8 vga_c : 5;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 bb_power_c : 6;
	u8 rsvd_5_1 : 1;
	u8 lna_h_c : 1;
#else
	u8 lna_h_c : 1;
	u8 rsvd_5_1 : 1;
	u8 bb_power_c : 6;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 gain_c : 6;
	u8 rsvd_5_2 : 2;
#else
	u8 rsvd_5_2 : 2;
	u8 gain_c : 6;
#endif

	/* @DW6 : Offset 24 */
	u8 pwdb_d;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 vga_d : 5;
	u8 lna_l_d : 3;
#else
	u8 lna_l_d : 3;
	u8 vga_d : 5;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 bb_power_d : 6;
	u8 rsvd_6_1 : 1;
	u8 lna_h_d : 1;
#else
	u8 lna_h_d : 1;
	u8 rsvd_6_1 : 1;
	u8 bb_power_d : 6;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 gain_d : 6;
	u8 rsvd_6_2 : 2;
#else
	u8 rsvd_6_2 : 2;
	u8 gain_d : 6;
#endif
};

__PACK struct phy_sts_rpt_jgr3_type1 {
/* @DW0 : Offset 0 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 page_num : 4;
	u8 pkt_cnt : 2;
	u8 channel_pri_msb : 2;
#else
	u8 channel_pri_msb : 2;
	u8 pkt_cnt : 2;
	u8 page_num : 4;
#endif
	u8 pwdb_a;
	u8 pwdb_b;
	u8 pwdb_c;

	/* @DW1 : Offset 4 */
	u8 pwdb_d;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 l_rxsc : 4;
	u8 ht_rxsc : 4;
#else
	u8 ht_rxsc : 4;
	u8 l_rxsc : 4;
#endif
	u8 channel_pri_lsb;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 band : 2;
	u8 rsvd_0 : 2;
	u8 gnt_bt : 1;
	u8 ldpc : 1;
	u8 stbc : 1;
	u8 beamformed : 1;
#else
	u8 beamformed : 1;
	u8 stbc : 1;
	u8 ldpc : 1;
	u8 gnt_bt : 1;
	u8 rsvd_0 : 2;
	u8 band : 2;
#endif

	/* @DW2 : Offset 8 */
	u8 channel_sec_lsb;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 channel_sec_msb : 2;
	u8 rsvd_1 : 2;
	u8 hw_antsw_occur_a : 1;
	u8 hw_antsw_occur_b : 1;
	u8 hw_antsw_occur_c : 1;
	u8 hw_antsw_occur_d : 1;
#else
	u8 hw_antsw_occur_d : 1;
	u8 hw_antsw_occur_c : 1;
	u8 hw_antsw_occur_b : 1;
	u8 hw_antsw_occur_a : 1;
	u8 rsvd_1 : 2;
	u8 channel_sec_msb : 2;

#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_a : 4;
	u8 antidx_b : 4;
#else
	u8 antidx_b : 4;
	u8 antidx_a : 4;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_c : 4;
	u8 antidx_d : 4;
#else
	u8 antidx_d : 4;
	u8 antidx_c : 4;
#endif

	/* @DW3 : Offset 12 */
	u8 paid;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 paid_msb : 1;
	u8 gid : 6;
	u8 rsvd_3 : 1;
#else
	u8 rsvd_3 : 1;
	u8 gid : 6;
	u8 paid_msb : 1;
#endif
	u16 rsvd_4;
#if 0
	/*@
	u8		rsvd_4;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8		rsvd_5: 6;
	u8		rf_mode: 2;
#else
	u8		rf_mode: 2;
	u8		rsvd_5: 6;
#endif
*/
#endif
	/* @DW4 : Offset 16 */
	s8 rxevm[4]; /* s(8,1) */

	/* @DW5 : Offset 20 */
	s8 cfo_tail[4]; /* s(8,7) */

	/* @DW6 : Offset 24 */
	s8 rxsnr[4]; /* s(8,1) */
};

__PACK struct phy_sts_rpt_jgr3_type2_3 {
/* Type2 is primary channel & type3 is secondary channel */
/* @DW0 and DW1: Offest 0 and Offset 4 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 page_num : 4;
	u8 pkt_cnt : 2;
	u8 channel_msb : 2;
#else
	u8 channel_msb : 2;
	u8 pkt_cnt : 2;
	u8 page_num : 4;
#endif
	u8 pwdb[4];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 l_rxsc : 4;
	u8 ht_rxsc : 4;
#else
	u8 ht_rxsc : 4;
	u8 l_rxsc : 4;
#endif
	u8 channel_lsb;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 band : 2;
	u8 rsvd_0 : 2;
	u8 gnt_bt : 1;
	u8 ldpc : 1;
	u8 stbc : 1;
	u8 beamformed : 1;
#else
	u8 beamformed : 1;
	u8 stbc : 1;
	u8 ldpc : 1;
	u8 gnt_bt : 1;
	u8 rsvd_0 : 2;
	u8 band : 2;
#endif

/* @DW2 : Offset 8 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 shift_l_map : 6;
	u8 rsvd_1 : 2;
#else
	u8 rsvd_1 : 2;
	u8 shift_l_map : 6;
#endif
	s8 pwed_th; /* @dynamic energy threshold S(8,2) */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 agc_table_a : 4;
	u8 agc_table_b : 4;
#else
	u8 agc_table_b : 4;
	u8 agc_table_a : 4;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 agc_table_c : 4;
	u8 agc_table_d : 4;
#else
	u8 agc_table_d : 4;
	u8 agc_table_c : 4;
#endif

	/* @DW3 : Offset 12 */
	u8 cnt_cca2agc_rdy; /* Time(ns) = cnt_cca2agc_ready*25 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 mp_gain_a : 6;
	u8 mp_gain_b_lsb : 2;
#else
	u8 mp_gain_b_lsb : 2;
	u8 mp_gain_a : 6;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 mp_gain_b_msb : 4;
	u8 mp_gain_c_lsb : 4;
#else
	u8 mp_gain_c_lsb : 4;
	u8 mp_gain_b_msb : 4;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 mp_gain_c_msb : 2;
	u8 avg_noise_pwr_lsb : 4;
	u8 rsvd_3 : 2;
	/* u8		r_rfmod:2; */
#else
	/* u8		r_rfmod:2; */
	u8 rsvd_3 : 2;
	u8 avg_noise_pwr_lsb : 4;
	u8 mp_gain_c_msb : 2;
#endif
	/* @DW4 ~ 5: offset 16 ~20 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 mp_gain_d : 6;
	u8 is_freq_select_fading : 1;
	u8 rsvd_2 : 1;
#else
	u8 rsvd_2 : 1;
	u8 is_freq_select_fading : 1;
	u8 mp_gain_d : 6;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 aagc_step_a : 2;
	u8 aagc_step_b : 2;
	u8 aagc_step_c : 2;
	u8 aagc_step_d : 2;
#else
	u8 aagc_step_d : 2;
	u8 aagc_step_c : 2;
	u8 aagc_step_b : 2;
	u8 aagc_step_a : 2;
#endif
	u8 ht_aagc_gain[4];
	u8 dagc_gain[4];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 counter : 6;
	u8 syn_count_lsb : 2;
#else
	u8 syn_count_lsb : 2;
	u8 counter : 6;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 syn_count_msb : 3;
	u8 avg_noise_pwr_msb : 5;
#else
	u8 avg_noise_pwr_msb : 5;
	u8 syn_count_msb : 3;
#endif
};

__PACK struct phy_sts_rpt_jgr3_type4 {
/* smart antenna */
/* @DW0 and DW1 : offset 0 and 4  */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 page_num : 4;
	u8 pkt_cnt : 2;
	u8 channel_msb : 2;
#else
	u8 channel_msb : 2;
	u8 pkt_cnt : 2;
	u8 page_num : 4;
#endif
	u8 pwdb[4];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 l_rxsc : 4;
	u8 ht_rxsc : 4;
#else
	u8 ht_rxsc : 4;
	u8 l_rxsc : 4;
#endif
	u8 channel_lsb;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 band : 2;
	u8 rsvd_0 : 2;
	u8 gnt_bt : 1;
	u8 ldpc : 1;
	u8 stbc : 1;
	u8 beamformed : 1;
#else
	u8 beamformed : 1;
	u8 stbc : 1;
	u8 ldpc : 1;
	u8 gnt_bt : 1;
	u8 rsvd_0 : 1;
	u8 band : 2;
#endif

/* @DW2 : offset 8 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 bad_tone_cnt_min_eign_0 : 4;
	u8 bad_tone_cnt_cn_excess_0 : 4;
#else
	u8 bad_tone_cnt_cn_excess_0 : 4;
	u8 bad_tone_cnt_min_eign_0 : 4;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 training_done_a : 1;
	u8 training_done_b : 1;
	u8 training_done_c : 1;
	u8 training_done_d : 1;
	u8 hw_antsw_occur_a : 1;
	u8 hw_antsw_occur_b : 1;
	u8 hw_antsw_occur_c : 1;
	u8 hw_antsw_occur_d : 1;
#else
	u8 hw_antsw_occur_d : 1;
	u8 hw_antsw_occur_c : 1;
	u8 hw_antsw_occur_b : 1;
	u8 hw_antsw_occur_a : 1;
	u8 training_done_d : 1;
	u8 training_done_c : 1;
	u8 training_done_b : 1;
	u8 training_done_a : 1;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_a : 4;
	u8 antidx_b : 4;
#else
	u8 antidx_b : 4;
	u8 antidx_a : 4;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_c : 4;
	u8 antidx_d : 4;
#else
	u8 antidx_d : 4;
	u8 antidx_c : 4;
#endif
/* @DW3 : offset 12 */
	u8 tx_pkt_cnt;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 bad_tone_cnt_min_eign_1 : 4;
	u8 bad_tone_cnt_cn_excess_1 : 4;
#else
	u8 bad_tone_cnt_cn_excess_1 : 4;
	u8 bad_tone_cnt_min_eign_1 : 4;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 avg_cond_num_0 : 7;
	u8 avg_cond_num_1_lsb : 1;
#else
	u8 avg_cond_num_1_lsb : 1;
	u8 avg_cond_num_0 : 7;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 avg_cond_num_1_msb : 6;
	u8 rsvd_1 : 2;
#else
	u8 rsvd_1 : 2;
	u8 avg_cond_num_1_msb : 6;
#endif

	/* @DW4 : offset 16 */
	s8 rxevm[4]; /* s(8,1) */

	/* @DW5 : offset 20 */
	u8 eigenvalue[4]; /* @eigenvalue or eigenvalue of seg0 (in dB) */

	/* @DW6 : ofset 24 */
	s8 rxsnr[4]; /* s(8,1) */
};

__PACK struct phy_sts_rpt_jgr3_type5 {
/* @Debug */
/* @DW0 ane DW1 : offset 0 and 4 */
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 page_num : 4;
	u8 pkt_cnt : 2;
	u8 channel_msb : 2;
#else
	u8 channel_msb : 2;
	u8 pkt_cnt : 2;
	u8 page_num : 4;
#endif
	u8 pwdb[4];
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 l_rxsc : 4;
	u8 ht_rxsc : 4;
#else
	u8 ht_rxsc : 4;
	u8 l_rxsc : 4;
#endif
	u8 channel_lsb;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 band : 2;
	u8 rsvd_0 : 2;
	u8 gnt_bt : 1;
	u8 ldpc : 1;
	u8 stbc : 1;
	u8 beamformed : 1;
#else
	u8 beamformed : 1;
	u8 stbc : 1;
	u8 ldpc : 1;
	u8 gnt_bt : 1;
	u8 rsvd_0 : 2;
	u8 band : 2;
#endif
	/* @DW2 : offset 8 */
	u8 rsvd_1;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 rsvd_2 : 4;
	u8 hw_antsw_occur_a : 1;
	u8 hw_antsw_occur_b : 1;
	u8 hw_antsw_occur_c : 1;
	u8 hw_antsw_occur_d : 1;
#else
	u8 hw_antsw_occur_d : 1;
	u8 hw_antsw_occur_c : 1;
	u8 hw_antsw_occur_b : 1;
	u8 hw_antsw_occur_a : 1;
	u8 rsvd_2 : 4;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_a : 4;
	u8 antidx_b : 4;
#else
	u8 antidx_b : 4;
	u8 antidx_a : 4;
#endif
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 antidx_c : 4;
	u8 antidx_d : 4;
#else
	u8 antidx_d : 4;
	u8 antidx_c : 4;
#endif
	/* @DW3 : offset 12 */
	u8 tx_pkt_cnt;
#if (ODM_ENDIAN_TYPE == ODM_ENDIAN_LITTLE)
	u8 inf_pos_0_A_flg : 1;
	u8 inf_pos_1_A_flg : 1;
	u8 inf_pos_0_B_flg : 1;
	u8 inf_pos_1_B_flg : 1;
	u8 inf_pos_0_C_flg : 1;
	u8 inf_pos_1_C_flg : 1;
	u8 inf_pos_0_D_flg : 1;
	u8 inf_pos_1_D_flg : 1;
#else
	u8 inf_pos_1_D_flg : 1;
	u8 inf_pos_0_D_flg : 1;
	u8 inf_pos_1_C_flg : 1;
	u8 inf_pos_0_C_flg : 1;
	u8 inf_pos_1_B_flg : 1;
	u8 inf_pos_0_B_flg : 1;
	u8 inf_pos_1_A_flg : 1;
	u8 inf_pos_0_A_flg : 1;
#endif
	u8 rsvd_3;
	u8 rsvd_4;
	/* @DW4 : offset 16 */
	u8 inf_pos_0_a;
	u8 inf_pos_1_a;
	u8 inf_pos_0_b;
	u8 inf_pos_1_b;
	/* @DW5 : offset 20 */
	u8 inf_pos_0_c;
	u8 inf_pos_1_c;
	u8 inf_pos_0_d;
	u8 inf_pos_1_d;
};
#endif /*@#ifdef PHYSTS_3RD_TYPE_SUPPORT*/

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
boolean
phydm_query_is_mu_api(struct dm_struct *phydm, u8 ppdu_idx, u8 *p_data_rate,
		      u8 *p_gid);
#endif

#ifdef PHYSTS_3RD_TYPE_SUPPORT
void phydm_rx_physts_3rd_type(void *dm_void, u8 *phy_sts,
			      struct phydm_perpkt_info_struct *pktinfo,
			      struct phydm_phyinfo_struct *phy_info);
#endif

void phydm_reset_phystatus_avg(struct dm_struct *dm);

void phydm_reset_phystatus_statistic(struct dm_struct *dm);

void phydm_reset_rssi_for_dm(struct dm_struct *dm, u8 station_id);

void phydm_get_cck_rssi_table_from_reg(struct dm_struct *dm);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void phydm_normal_driver_rx_sniffer(
	struct dm_struct *dm,
	u8 *desc,
	PRT_RFD_STATUS rt_rfd_status,
	u8 *drv_info,
	u8 phy_status);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
s32 phydm_signal_scale_mapping(struct dm_struct *dm, s32 curr_sig);
#endif

void odm_phy_status_query(struct dm_struct *dm,
			  struct phydm_phyinfo_struct *phy_info,
			  u8 *phy_status_inf,
			  struct phydm_perpkt_info_struct *pktinfo);

void phydm_rx_phy_status_init(void *dm_void);

void phydm_physts_dbg(void *dm_void, char input[][16], u32 *_used,
		      char *output, u32 *_out_len);

#endif /*@#ifndef	__HALHWOUTSRC_H__*/
