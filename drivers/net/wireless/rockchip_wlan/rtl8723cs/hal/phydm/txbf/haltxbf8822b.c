/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
 *****************************************************************************/
/*@============================================================*/
/* @Description:                                              */
/*                                                           @*/
/* This file is for 8814A TXBF mechanism                     */
/*                                                           @*/
/*@============================================================*/

#include "mp_precomp.h"
#include "phydm_precomp.h"

#if (RTL8822B_SUPPORT == 1)
#ifdef PHYDM_BEAMFORMING_SUPPORT

u8 hal_txbf_8822b_get_ntx(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 ntx = 0;

#if DEV_BUS_TYPE == RT_USB_INTERFACE
	if (dm->support_interface == ODM_ITRF_USB) {
		if (*dm->hub_usb_mode == 2) { /*USB3.0*/
			if (dm->rf_type == RF_4T4R)
				ntx = 3;
			else if (dm->rf_type == RF_3T3R)
				ntx = 2;
			else
				ntx = 1;
		} else if (*dm->hub_usb_mode == 1) /*USB 2.0 always 2Tx*/
			ntx = 1;
		else
			ntx = 1;
	} else
#endif
	{
		if (dm->rf_type == RF_4T4R)
			ntx = 3;
		else if (dm->rf_type == RF_3T3R)
			ntx = 2;
		else
			ntx = 1;
	}

	return ntx;
}

u8 hal_txbf_8822b_get_nrx(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 nrx = 0;

	if (dm->rf_type == RF_4T4R)
		nrx = 3;
	else if (dm->rf_type == RF_3T3R)
		nrx = 2;
	else if (dm->rf_type == RF_2T2R)
		nrx = 1;
	else if (dm->rf_type == RF_2T3R)
		nrx = 2;
	else if (dm->rf_type == RF_2T4R)
		nrx = 3;
	else if (dm->rf_type == RF_1T1R)
		nrx = 0;
	else if (dm->rf_type == RF_1T2R)
		nrx = 1;
	else
		nrx = 0;

	return nrx;
}

/***************SU & MU BFee Entry********************/
void hal_txbf_8822b_rf_mode(
	void *dm_void,
	struct _RT_BEAMFORMING_INFO *beamforming_info,
	u8 idx)
{
#if 0
	struct dm_struct	*dm = (struct dm_struct *)dm_void;
	u8				i, nr_index = 0;
	boolean				is_self_beamformer = false;
	boolean				is_self_beamformee = false;
	struct _RT_BEAMFORMEE_ENTRY	beamformee_entry;

	if (idx < BEAMFORMEE_ENTRY_NUM)
		beamformee_entry = beamforming_info->beamformee_entry[idx];
	else
		return;

	if (dm->rf_type == RF_1T1R)
		return;

	for (i = RF_PATH_A; i < RF_PATH_B; i++) {
		odm_set_rf_reg(dm, (enum rf_path)i, rf_welut_jaguar, 0x80000, 0x1);
		/*RF mode table write enable*/
	}

	if (beamforming_info->beamformee_su_cnt > 0 || beamforming_info->beamformee_mu_cnt > 0) {
		for (i = RF_PATH_A; i < RF_PATH_B; i++) {
			odm_set_rf_reg(dm, (enum rf_path)i, rf_mode_table_addr, 0xfffff, 0x18000);
			/*Select RX mode*/
			odm_set_rf_reg(dm, (enum rf_path)i, rf_mode_table_data0, 0xfffff, 0xBE77F);
			/*Set Table data*/
			odm_set_rf_reg(dm, (enum rf_path)i, rf_mode_table_data1, 0xfffff, 0x226BF);
			/*@Enable TXIQGEN in RX mode*/
		}
		odm_set_rf_reg(dm, RF_PATH_A, rf_mode_table_data1, 0xfffff, 0xE26BF);
		/*@Enable TXIQGEN in RX mode*/
	}

	for (i = RF_PATH_A; i < RF_PATH_B; i++) {
		odm_set_rf_reg(dm, (enum rf_path)i, rf_welut_jaguar, 0x80000, 0x0);
		/*RF mode table write disable*/
	}

	if (beamforming_info->beamformee_su_cnt > 0) {
		/*@for 8814 19ac(idx 1), 19b4(idx 0), different Tx ant setting*/
		odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF1_8822B, BIT(28) | BIT29, 0x2);			/*@enable BB TxBF ant mapping register*/

		if (idx == 0) {
			/*Nsts = 2	AB*/
			odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF0_8822B, 0xffff, 0x0433);
			odm_set_bb_reg(dm, REG_BB_TX_PATH_SEL_1_8822B, 0xfff00000, 0x043);
			/*odm_set_bb_reg(dm, REG_BB_TX_PATH_SEL_2, MASKLWORD, 0x430);*/

		} else {/*@IDX =1*/
			odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF1_8822B, 0xffff, 0x0433);
			odm_set_bb_reg(dm, REG_BB_TX_PATH_SEL_1_8822B, 0xfff00000, 0x043);
			/*odm_set_bb_reg(dm, REG_BB_TX_PATH_SEL_2, MASKLWORD, 0x430;*/
		}
	} else {
		odm_set_bb_reg(dm, REG_BB_TX_PATH_SEL_1_8822B, 0xfff00000, 0x1); /*@1SS by path-A*/
		odm_set_bb_reg(dm, REG_BB_TX_PATH_SEL_2_8822B, MASKLWORD, 0x430); /*@2SS by path-A,B*/
	}

	if (beamforming_info->beamformee_mu_cnt > 0) {
		/*@MU STAs share the common setting*/
		odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF1_8822B, BIT(31), 1);
		odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF1_8822B, 0xffff, 0x0433);
		odm_set_bb_reg(dm, REG_BB_TX_PATH_SEL_1_8822B, 0xfff00000, 0x043);
	}
#endif
}
#if 0
void
hal_txbf_8822b_download_ndpa(
	void			*adapter,
	u8				idx
)
{
	u8			u1b_tmp = 0, tmp_reg422 = 0;
	u8			bcn_valid_reg = 0, count = 0, dl_bcn_count = 0;
	u16			head_page = 0x7FE;
	boolean			is_send_beacon = false;
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(adapter);
	u16			tx_page_bndy = LAST_ENTRY_OF_TX_PKT_BUFFER_8814A; /*@default reseved 1 page for the IC type which is undefined.*/
	struct _RT_BEAMFORMING_INFO	*beam_info = GET_BEAMFORM_INFO(adapter);
	struct _RT_BEAMFORMEE_ENTRY	*p_beam_entry = beam_info->beamformee_entry + idx;

	hal_data->is_fw_dw_rsvd_page_in_progress = true;
	phydm_get_hal_def_var_handler_interface(dm, HAL_DEF_TX_PAGE_BOUNDARY, (u16 *)&tx_page_bndy);

	/*Set REG_CR bit 8. DMA beacon by SW.*/
	u1b_tmp = platform_efio_read_1byte(adapter, REG_CR_8814A + 1);
	platform_efio_write_1byte(adapter,  REG_CR_8814A + 1, (u1b_tmp | BIT(0)));


	/*Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.*/
	tmp_reg422 = platform_efio_read_1byte(adapter, REG_FWHW_TXQ_CTRL_8814A + 2);
	platform_efio_write_1byte(adapter, REG_FWHW_TXQ_CTRL_8814A + 2,  tmp_reg422 & (~BIT(6)));

	if (tmp_reg422 & BIT(6)) {
		RT_TRACE(COMP_INIT, DBG_LOUD, ("SetBeamformDownloadNDPA_8814A(): There is an adapter is sending beacon.\n"));
		is_send_beacon = true;
	}

	/*@0x204[11:0]	Beacon Head for TXDMA*/
	platform_efio_write_2byte(adapter, REG_FIFOPAGE_CTRL_2_8814A, head_page);

	do {
		/*@Clear beacon valid check bit.*/
		bcn_valid_reg = platform_efio_read_1byte(adapter, REG_FIFOPAGE_CTRL_2_8814A + 1);
		platform_efio_write_1byte(adapter, REG_FIFOPAGE_CTRL_2_8814A + 1, (bcn_valid_reg | BIT(7)));

		/*@download NDPA rsvd page.*/
		if (p_beam_entry->beamform_entry_cap & BEAMFORMER_CAP_VHT_SU)
			beamforming_send_vht_ndpa_packet(dm, p_beam_entry->mac_addr, p_beam_entry->AID, p_beam_entry->sound_bw, BEACON_QUEUE);
		else
			beamforming_send_ht_ndpa_packet(dm, p_beam_entry->mac_addr, p_beam_entry->sound_bw, BEACON_QUEUE);

		/*@check rsvd page download OK.*/
		bcn_valid_reg = platform_efio_read_1byte(adapter, REG_FIFOPAGE_CTRL_2_8814A + 1);
		count = 0;
		while (!(bcn_valid_reg & BIT(7)) && count < 20) {
			count++;
			delay_us(10);
			bcn_valid_reg = platform_efio_read_1byte(adapter, REG_FIFOPAGE_CTRL_2_8814A + 2);
		}
		dl_bcn_count++;
	} while (!(bcn_valid_reg & BIT(7)) && dl_bcn_count < 5);

	if (!(bcn_valid_reg & BIT(0)))
		RT_DISP(FBEAM, FBEAM_ERROR, ("%s Download RSVD page failed!\n", __func__));

	/*@0x204[11:0]	Beacon Head for TXDMA*/
	platform_efio_write_2byte(adapter, REG_FIFOPAGE_CTRL_2_8814A, tx_page_bndy);

	/*To make sure that if there exists an adapter which would like to send beacon.*/
	/*@If exists, the origianl value of 0x422[6] will be 1, we should check this to*/
	/*prevent from setting 0x422[6] to 0 after download reserved page, or it will cause */
	/*the beacon cannot be sent by HW.*/
	/*@2010.06.23. Added by tynli.*/
	if (is_send_beacon)
		platform_efio_write_1byte(adapter, REG_FWHW_TXQ_CTRL_8814A + 2, tmp_reg422);

	/*@Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli.*/
	/*@Clear CR[8] or beacon packet will not be send to TxBuf anymore.*/
	u1b_tmp = platform_efio_read_1byte(adapter, REG_CR_8814A + 1);
	platform_efio_write_1byte(adapter, REG_CR_8814A + 1, (u1b_tmp & (~BIT(0))));

	p_beam_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSED;

	hal_data->is_fw_dw_rsvd_page_in_progress = false;
}

void
hal_txbf_8822b_fw_txbf_cmd(
	void	*adapter
)
{
	u8	idx, period = 0;
	u8	PageNum0 = 0xFF, PageNum1 = 0xFF;
	u8	u1_tx_bf_parm[3] = {0};

	PMGNT_INFO				mgnt_info = &(adapter->MgntInfo);
	struct _RT_BEAMFORMING_INFO	*beam_info = GET_BEAMFORM_INFO(adapter);

	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		if (beam_info->beamformee_entry[idx].is_used && beam_info->beamformee_entry[idx].beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			if (beam_info->beamformee_entry[idx].is_sound) {
				PageNum0 = 0xFE;
				PageNum1 = 0x07;
				period = (u8)(beam_info->beamformee_entry[idx].sound_period);
			} else if (PageNum0 == 0xFF) {
				PageNum0 = 0xFF; /*stop sounding*/
				PageNum1 = 0x0F;
			}
		}
	}

	u1_tx_bf_parm[0] = PageNum0;
	u1_tx_bf_parm[1] = PageNum1;
	u1_tx_bf_parm[2] = period;
	fill_h2c_cmd(adapter, PHYDM_H2C_TXBF, 3, u1_tx_bf_parm);

	RT_DISP(FBEAM, FBEAM_FUN, ("@%s End, PageNum0 = 0x%x, PageNum1 = 0x%x period = %d", __func__, PageNum0, PageNum1, period));
}
#endif

#if 0
void
hal_txbf_8822b_init(
	void			*dm_void
)
{
	struct dm_struct	*dm = (struct dm_struct *)dm_void;
	u8		u1b_tmp;
	struct _RT_BEAMFORMING_INFO		*beamforming_info = &dm->beamforming_info;
	void				*adapter = dm->adapter;

	odm_set_bb_reg(dm, R_0x14c0, BIT(16), 1); /*@Enable P1 aggr new packet according to P0 transfer time*/
	odm_set_bb_reg(dm, R_0x14c0, BIT(15) | BIT14 | BIT13 | BIT12, 10); /*@MU Retry Limit*/
	odm_set_bb_reg(dm, R_0x14c0, BIT(7), 0); /*@Disable Tx MU-MIMO until sounding done*/
	odm_set_bb_reg(dm, R_0x14c0, 0x3F, 0); /* @Clear validity of MU STAs */
	odm_write_1byte(dm, 0x167c, 0x70); /*@MU-MIMO Option as default value*/
	odm_write_2byte(dm, 0x1680, 0); /*@MU-MIMO Control as default value*/

	/* Set MU NDPA rate & BW source */
	/* @0x42C[30] = 1 (0: from Tx desc, 1: from 0x45F) */
	u1b_tmp = odm_read_1byte(dm, 0x42C);
	odm_write_1byte(dm, REG_TXBF_CTRL_8822B, (u1b_tmp | BIT(6)));
	/* @0x45F[7:0] = 0x10 (rate=OFDM_6M, BW20) */
	odm_write_1byte(dm, REG_NDPA_OPT_CTRL_8822B, 0x10);

	/*Temp Settings*/
	odm_set_bb_reg(dm, R_0x6dc, 0x3F000000, 4); /*STA2's CSI rate is fixed at 6M*/
	odm_set_bb_reg(dm, R_0x1c94, MASKDWORD, 0xAFFFAFFF); /*@Grouping bitmap parameters*/

	/* @Init HW variable */
	beamforming_info->reg_mu_tx_ctrl = odm_read_4byte(dm, 0x14c0);

	if (dm->rf_type == RF_2T2R) { /*@2T2R*/
		PHYDM_DBG(dm, DBG_TXBF, "%s: rf_type is 2T2R\n", __func__);
		config_phydm_trx_mode_8822b(dm, (enum bb_path)3,
					    (enum bb_path)3, BB_PATH_AB;
	}

#if (OMNIPEEK_SNIFFER_ENABLED == 1)
	/* @Config HW to receive packet on the user position from registry for sniffer mode. */
	/* odm_set_bb_reg(dm, R_0xb00, BIT(9), 1);*/ /* For A-cut only. RegB00[9] = 1 (enable PMAC Rx) */
	odm_set_bb_reg(dm, R_0xb54, BIT(30), 1); /* RegB54[30] = 1 (force user position) */
	odm_set_bb_reg(dm, R_0xb54, (BIT(29) | BIT28), adapter->MgntInfo.sniff_user_position); /* RegB54[29:28] = user position (0~3) */
	PHYDM_DBG(dm, DBG_TXBF,
		  "Set adapter->MgntInfo.sniff_user_position=%#X\n",
		  adapter->MgntInfo.sniff_user_position);
#endif
}
#endif

void hal_txbf_8822b_enter(
	void *dm_void,
	u8 bfer_bfee_idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i = 0;
	u8 bfer_idx = (bfer_bfee_idx & 0xF0) >> 4;
	u8 bfee_idx = (bfer_bfee_idx & 0xF);
	u16 csi_param = 0;
	struct _RT_BEAMFORMING_INFO *beamforming_info = &dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY *p_beamformee_entry;
	struct _RT_BEAMFORMER_ENTRY *beamformer_entry;
	u16 value16, sta_id = 0;
	u8 nc_index = 0, nr_index = 0, grouping = 0, codebookinfo = 0, coefficientsize = 0;
	u32 gid_valid, user_position_l, user_position_h;
	u32 mu_reg[6] = {0x1684, 0x1686, 0x1688, 0x168a, 0x168c, 0x168e};
	u8 u1b_tmp;
	u32 u4b_tmp;

	RT_DISP(FBEAM, FBEAM_FUN, ("%s: bfer_bfee_idx=%d, bfer_idx=%d, bfee_idx=%d\n", __func__, bfer_bfee_idx, bfer_idx, bfee_idx));

	/*************SU BFer Entry Init*************/
	if (beamforming_info->beamformer_su_cnt > 0 && bfer_idx < BEAMFORMER_ENTRY_NUM) {
		beamformer_entry = &beamforming_info->beamformer_entry[bfer_idx];
		beamformer_entry->is_mu_ap = false;
		/*Sounding protocol control*/
		odm_write_1byte(dm, REG_SND_PTCL_CTRL_8822B, 0xDB);

		for (i = 0; i < MAX_BEAMFORMER_SU; i++) {
			if ((beamforming_info->beamformer_su_reg_maping & BIT(i)) == 0) {
				beamforming_info->beamformer_su_reg_maping |= BIT(i);
				beamformer_entry->su_reg_index = i;
				break;
			}
		}

		/*@MAC address/Partial AID of Beamformer*/
		if (beamformer_entry->su_reg_index == 0) {
			for (i = 0; i < 6; i++)
				odm_write_1byte(dm, (REG_ASSOCIATED_BFMER0_INFO_8822B + i), beamformer_entry->mac_addr[i]);
		} else {
			for (i = 0; i < 6; i++)
				odm_write_1byte(dm, (REG_ASSOCIATED_BFMER1_INFO_8822B + i), beamformer_entry->mac_addr[i]);
		}

		/*@CSI report parameters of Beamformer*/
		nc_index = hal_txbf_8822b_get_nrx(dm); /*@for 8814A nrx = 3(4 ant), min=0(1 ant)*/
		nr_index = beamformer_entry->num_of_sounding_dim; /*@0x718[7] = 1 use Nsts, 0x718[7] = 0 use reg setting. as Bfee, we use Nsts, so nr_index don't care*/

		grouping = 0;

		/*@for ac = 1, for n = 3*/
		if (beamformer_entry->beamform_entry_cap & BEAMFORMEE_CAP_VHT_SU)
			codebookinfo = 1;
		else if (beamformer_entry->beamform_entry_cap & BEAMFORMEE_CAP_HT_EXPLICIT)
			codebookinfo = 3;

		coefficientsize = 3;

		csi_param = (u16)((coefficientsize << 10) | (codebookinfo << 8) | (grouping << 6) | (nr_index << 3) | (nc_index));

		if (bfer_idx == 0)
			odm_write_2byte(dm, REG_TX_CSI_RPT_PARAM_BW20_8822B, csi_param);
		else
			odm_write_2byte(dm, REG_TX_CSI_RPT_PARAM_BW20_8822B + 2, csi_param);
		/*ndp_rx_standby_timer, 8814 need > 0x56, suggest from Dvaid*/
		odm_write_1byte(dm, REG_SND_PTCL_CTRL_8822B + 3, 0x70);
	}

	/*************SU BFee Entry Init*************/
	if (beamforming_info->beamformee_su_cnt > 0 && bfee_idx < BEAMFORMEE_ENTRY_NUM) {
		p_beamformee_entry = &beamforming_info->beamformee_entry[bfee_idx];
		p_beamformee_entry->is_mu_sta = false;
		hal_txbf_8822b_rf_mode(dm, beamforming_info, bfee_idx);

		if (phydm_acting_determine(dm, phydm_acting_as_ibss))
			sta_id = p_beamformee_entry->mac_id;
		else
			sta_id = p_beamformee_entry->p_aid;

		for (i = 0; i < MAX_BEAMFORMEE_SU; i++) {
			if ((beamforming_info->beamformee_su_reg_maping & BIT(i)) == 0) {
				beamforming_info->beamformee_su_reg_maping |= BIT(i);
				p_beamformee_entry->su_reg_index = i;
				break;
			}
		}

		/*P_AID of Beamformee & enable NDPA transmission & enable NDPA interrupt*/
		if (p_beamformee_entry->su_reg_index == 0) {
			odm_write_2byte(dm, REG_TXBF_CTRL_8822B, sta_id);
			odm_write_1byte(dm, REG_TXBF_CTRL_8822B + 3, odm_read_1byte(dm, REG_TXBF_CTRL_8822B + 3) | BIT(4) | BIT(6) | BIT(7));
		} else
			odm_write_2byte(dm, REG_TXBF_CTRL_8822B + 2, sta_id | BIT(14) | BIT(15) | BIT(12));

		/*@CSI report parameters of Beamformee*/
		if (p_beamformee_entry->su_reg_index == 0) {
			/*@Get BIT24 & BIT25*/
			u8 tmp = odm_read_1byte(dm, REG_ASSOCIATED_BFMEE_SEL_8822B + 3) & 0x3;

			odm_write_1byte(dm, REG_ASSOCIATED_BFMEE_SEL_8822B + 3, tmp | 0x60);
			odm_write_2byte(dm, REG_ASSOCIATED_BFMEE_SEL_8822B, sta_id | BIT(9));
		} else
			odm_write_2byte(dm, REG_ASSOCIATED_BFMEE_SEL_8822B + 2, sta_id | 0xE200); /*Set BIT25*/

		phydm_beamforming_notify(dm);
	}

	/*************MU BFer Entry Init*************/
	if (beamforming_info->beamformer_mu_cnt > 0 && bfer_idx < BEAMFORMER_ENTRY_NUM) {
		beamformer_entry = &beamforming_info->beamformer_entry[bfer_idx];
		beamforming_info->mu_ap_index = bfer_idx;
		beamformer_entry->is_mu_ap = true;
		for (i = 0; i < 8; i++)
			beamformer_entry->gid_valid[i] = 0;
		for (i = 0; i < 16; i++)
			beamformer_entry->user_position[i] = 0;

		/*Sounding protocol control*/
		odm_write_1byte(dm, REG_SND_PTCL_CTRL_8822B, 0xDB);

		/* @MAC address */
		for (i = 0; i < 6; i++)
			odm_write_1byte(dm, (REG_ASSOCIATED_BFMER0_INFO_8822B + i), beamformer_entry->mac_addr[i]);

		/* Set partial AID */
		odm_write_2byte(dm, (REG_ASSOCIATED_BFMER0_INFO_8822B + 6), beamformer_entry->p_aid);

		/* @Fill our AID to 0x1680[11:0] and [13:12] = 2b'00, BF report segment select to 3895 bytes*/
		u1b_tmp = odm_read_1byte(dm, 0x1680);
		u1b_tmp = (beamformer_entry->p_aid) & 0xFFF;
		odm_write_1byte(dm, 0x1680, u1b_tmp);

		/* Set 80us for leaving ndp_rx_standby_state */
		odm_write_1byte(dm, 0x71B, 0x50);

		/* Set 0x6A0[14] = 1 to accept action_no_ack */
		u1b_tmp = odm_read_1byte(dm, REG_RXFLTMAP0_8822B + 1);
		u1b_tmp |= 0x40;
		odm_write_1byte(dm, REG_RXFLTMAP0_8822B + 1, u1b_tmp);
		/* Set 0x6A2[5:4] = 1 to NDPA and BF report poll */
		u1b_tmp = odm_read_1byte(dm, REG_RXFLTMAP1_8822B);
		u1b_tmp |= 0x30;
		odm_write_1byte(dm, REG_RXFLTMAP1_8822B, u1b_tmp);

		/*@CSI report parameters of Beamformer*/
		nc_index = hal_txbf_8822b_get_nrx(dm); /* @Depend on RF type */
		nr_index = 1; /*@0x718[7] = 1 use Nsts, 0x718[7] = 0 use reg setting. as Bfee, we use Nsts, so nr_index don't care*/
		grouping = 0; /*no grouping*/
		codebookinfo = 1; /*@7 bit for psi, 9 bit for phi*/
		coefficientsize = 0; /*This is nothing really matter*/
		csi_param = (u16)((coefficientsize << 10) | (codebookinfo << 8) | (grouping << 6) | (nr_index << 3) | (nc_index));
		odm_write_2byte(dm, 0x6F4, csi_param);

		/*@for B-cut*/
		odm_set_bb_reg(dm, R_0x6a0, BIT(20), 0);
		odm_set_bb_reg(dm, R_0x688, BIT(20), 0);
	}

	/*************MU BFee Entry Init*************/
	if (beamforming_info->beamformee_mu_cnt > 0 && bfee_idx < BEAMFORMEE_ENTRY_NUM) {
		p_beamformee_entry = &beamforming_info->beamformee_entry[bfee_idx];
		p_beamformee_entry->is_mu_sta = true;
		for (i = 0; i < MAX_BEAMFORMEE_MU; i++) {
			if ((beamforming_info->beamformee_mu_reg_maping & BIT(i)) == 0) {
				beamforming_info->beamformee_mu_reg_maping |= BIT(i);
				p_beamformee_entry->mu_reg_index = i;
				break;
			}
		}

		if (p_beamformee_entry->mu_reg_index == 0xFF) {
			/* There is no valid bit in beamformee_mu_reg_maping */
			RT_DISP(FBEAM, FBEAM_FUN, ("%s: ERROR! There is no valid bit in beamformee_mu_reg_maping!\n", __func__));
			return;
		}

		/*User position table*/
		switch (p_beamformee_entry->mu_reg_index) {
		case 0:
			gid_valid = 0x7fe;
			user_position_l = 0x111110;
			user_position_h = 0x0;
			break;
		case 1:
			gid_valid = 0x7f806;
			user_position_l = 0x11000004;
			user_position_h = 0x11;
			break;
		case 2:
			gid_valid = 0x1f81818;
			user_position_l = 0x400040;
			user_position_h = 0x11100;
			break;
		case 3:
			gid_valid = 0x1e186060;
			user_position_l = 0x4000400;
			user_position_h = 0x1100040;
			break;
		case 4:
			gid_valid = 0x66618180;
			user_position_l = 0x40004000;
			user_position_h = 0x10040400;
			break;
		case 5:
			gid_valid = 0x79860600;
			user_position_l = 0x40000;
			user_position_h = 0x4404004;
			break;
		}

		for (i = 0; i < 8; i++) {
			if (i < 4) {
				p_beamformee_entry->gid_valid[i] = (u8)(gid_valid & 0xFF);
				gid_valid = (gid_valid >> 8);
			} else
				p_beamformee_entry->gid_valid[i] = 0;
		}
		for (i = 0; i < 16; i++) {
			if (i < 4)
				p_beamformee_entry->user_position[i] = (u8)((user_position_l >> (i * 8)) & 0xFF);
			else if (i < 8)
				p_beamformee_entry->user_position[i] = (u8)((user_position_h >> ((i - 4) * 8)) & 0xFF);
			else
				p_beamformee_entry->user_position[i] = 0;
		}

		/*Sounding protocol control*/
		odm_write_1byte(dm, REG_SND_PTCL_CTRL_8822B, 0xDB);

		/*select MU STA table*/
		beamforming_info->reg_mu_tx_ctrl &= ~(BIT(8) | BIT(9) | BIT(10));
		beamforming_info->reg_mu_tx_ctrl |= (p_beamformee_entry->mu_reg_index << 8) & (BIT(8) | BIT(9) | BIT(10));
		odm_write_4byte(dm, 0x14c0, beamforming_info->reg_mu_tx_ctrl);

		odm_set_bb_reg(dm, R_0x14c4, MASKDWORD, 0); /*Reset gid_valid table*/
		odm_set_bb_reg(dm, R_0x14c8, MASKDWORD, user_position_l);
		odm_set_bb_reg(dm, R_0x14cc, MASKDWORD, user_position_h);

		/*set validity of MU STAs*/
		beamforming_info->reg_mu_tx_ctrl &= 0xFFFFFFC0;
		beamforming_info->reg_mu_tx_ctrl |= beamforming_info->beamformee_mu_reg_maping & 0x3F;
		odm_write_4byte(dm, 0x14c0, beamforming_info->reg_mu_tx_ctrl);

		PHYDM_DBG(dm, DBG_TXBF,
			  "@%s, reg_mu_tx_ctrl = 0x%x, user_position_l = 0x%x, user_position_h = 0x%x\n",
			  __func__, beamforming_info->reg_mu_tx_ctrl,
			  user_position_l, user_position_h);

		value16 = odm_read_2byte(dm, mu_reg[p_beamformee_entry->mu_reg_index]);
		value16 &= 0xFE00; /*@Clear PAID*/
		value16 |= BIT(9); /*@Enable MU BFee*/
		value16 |= p_beamformee_entry->p_aid;
		odm_write_2byte(dm, mu_reg[p_beamformee_entry->mu_reg_index], value16);

		/* @0x42C[30] = 1 (0: from Tx desc, 1: from 0x45F) */
		u1b_tmp = odm_read_1byte(dm, REG_TXBF_CTRL_8822B + 3);
		u1b_tmp |= 0xD0; /* Set bit 28, 30, 31 to 3b'111*/
		odm_write_1byte(dm, REG_TXBF_CTRL_8822B + 3, u1b_tmp);
		/* Set NDPA to 6M*/
		odm_write_1byte(dm, REG_NDPA_RATE_8822B, 0x4);

		u1b_tmp = odm_read_1byte(dm, REG_NDPA_OPT_CTRL_8822B);
		u1b_tmp &= 0xFC; /* @Clear bit 0, 1*/
		odm_write_1byte(dm, REG_NDPA_OPT_CTRL_8822B, u1b_tmp);

		u4b_tmp = odm_read_4byte(dm, REG_SND_PTCL_CTRL_8822B);
		u4b_tmp = ((u4b_tmp & 0xFF0000FF) | 0x020200); /* Set [23:8] to 0x0202*/
		odm_write_4byte(dm, REG_SND_PTCL_CTRL_8822B, u4b_tmp);

		/* Set 0x6A0[14] = 1 to accept action_no_ack */
		u1b_tmp = odm_read_1byte(dm, REG_RXFLTMAP0_8822B + 1);
		u1b_tmp |= 0x40;
		odm_write_1byte(dm, REG_RXFLTMAP0_8822B + 1, u1b_tmp);
		/* @End of MAC registers setting */

		hal_txbf_8822b_rf_mode(dm, beamforming_info, bfee_idx);
#if (SUPPORT_MU_BF == 1)
		/*Special for plugfest*/
		delay_ms(50); /* wait for 4-way handshake ending*/
		send_sw_vht_gid_mgnt_frame(dm, p_beamformee_entry->mac_addr, bfee_idx);
#endif

		phydm_beamforming_notify(dm);
#if 1
		{
			u32 ctrl_info_offset, index;
			/*Set Ctrl Info*/
			odm_write_2byte(dm, 0x140, 0x660);
			ctrl_info_offset = 0x8000 + 32 * p_beamformee_entry->mac_id;
			/*Reset Ctrl Info*/
			for (index = 0; index < 8; index++)
				odm_write_4byte(dm, ctrl_info_offset + index * 4, 0);

			odm_write_4byte(dm, ctrl_info_offset, (p_beamformee_entry->mu_reg_index + 1) << 16);
			odm_write_1byte(dm, 0x81, 0x80); /*RPTBUF ready*/

			PHYDM_DBG(dm, DBG_TXBF,
				  "@%s, mac_id = %d, ctrl_info_offset = 0x%x, mu_reg_index = %x\n",
				  __func__, p_beamformee_entry->mac_id,
				  ctrl_info_offset,
				  p_beamformee_entry->mu_reg_index);
		}
#endif
	}
}

void hal_txbf_8822b_leave(
	void *dm_void,
	u8 idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beamforming_info = &dm->beamforming_info;
	struct _RT_BEAMFORMER_ENTRY *beamformer_entry;
	struct _RT_BEAMFORMEE_ENTRY *p_beamformee_entry;
	u32 mu_reg[6] = {0x1684, 0x1686, 0x1688, 0x168a, 0x168c, 0x168e};

	if (idx < BEAMFORMER_ENTRY_NUM) {
		beamformer_entry = &beamforming_info->beamformer_entry[idx];
		p_beamformee_entry = &beamforming_info->beamformee_entry[idx];
	} else
		return;

	/*@Clear P_AID of Beamformee*/
	/*@Clear MAC address of Beamformer*/
	/*@Clear Associated Bfmee Sel*/

	if (beamformer_entry->beamform_entry_cap == BEAMFORMING_CAP_NONE) {
		odm_write_1byte(dm, REG_SND_PTCL_CTRL_8822B, 0xD8);
		if (beamformer_entry->is_mu_ap == 0) { /*SU BFer */
			if (beamformer_entry->su_reg_index == 0) {
				odm_write_4byte(dm, REG_ASSOCIATED_BFMER0_INFO_8822B, 0);
				odm_write_2byte(dm, REG_ASSOCIATED_BFMER0_INFO_8822B + 4, 0);
				odm_write_2byte(dm, REG_TX_CSI_RPT_PARAM_BW20_8822B, 0);
			} else {
				odm_write_4byte(dm, REG_ASSOCIATED_BFMER1_INFO_8822B, 0);
				odm_write_2byte(dm, REG_ASSOCIATED_BFMER1_INFO_8822B + 4, 0);
				odm_write_2byte(dm, REG_TX_CSI_RPT_PARAM_BW20_8822B + 2, 0);
			}
			beamforming_info->beamformer_su_reg_maping &= ~(BIT(beamformer_entry->su_reg_index));
			beamformer_entry->su_reg_index = 0xFF;
		} else { /*@MU BFer */
			/*set validity of MU STA0 and MU STA1*/
			beamforming_info->reg_mu_tx_ctrl &= 0xFFFFFFC0;
			odm_write_4byte(dm, 0x14c0, beamforming_info->reg_mu_tx_ctrl);

			odm_memory_set(dm, beamformer_entry->gid_valid, 0, 8);
			odm_memory_set(dm, beamformer_entry->user_position, 0, 16);
			beamformer_entry->is_mu_ap = false;
		}
	}

	if (p_beamformee_entry->beamform_entry_cap == BEAMFORMING_CAP_NONE) {
		hal_txbf_8822b_rf_mode(dm, beamforming_info, idx);
		if (p_beamformee_entry->is_mu_sta == 0) { /*SU BFee*/
			if (p_beamformee_entry->su_reg_index == 0) {
				odm_write_2byte(dm, REG_TXBF_CTRL_8822B, 0x0);
				odm_write_1byte(dm, REG_TXBF_CTRL_8822B + 3, odm_read_1byte(dm, REG_TXBF_CTRL_8822B + 3) | BIT(4) | BIT(6) | BIT(7));
				odm_write_2byte(dm, REG_ASSOCIATED_BFMEE_SEL_8822B, 0);
			} else {
				odm_write_2byte(dm, REG_TXBF_CTRL_8822B + 2, 0x0 | BIT(14) | BIT(15) | BIT(12));

				odm_write_2byte(dm, REG_ASSOCIATED_BFMEE_SEL_8822B + 2,
						odm_read_2byte(dm, REG_ASSOCIATED_BFMEE_SEL_8822B + 2) & 0x60);
			}
			beamforming_info->beamformee_su_reg_maping &= ~(BIT(p_beamformee_entry->su_reg_index));
			p_beamformee_entry->su_reg_index = 0xFF;
		} else { /*@MU BFee */
			/*@Disable sending NDPA & BF-rpt-poll to this BFee*/
			odm_write_2byte(dm, mu_reg[p_beamformee_entry->mu_reg_index], 0);
			/*set validity of MU STA*/
			beamforming_info->reg_mu_tx_ctrl &= ~(BIT(p_beamformee_entry->mu_reg_index));
			odm_write_4byte(dm, 0x14c0, beamforming_info->reg_mu_tx_ctrl);

			p_beamformee_entry->is_mu_sta = false;
			beamforming_info->beamformee_mu_reg_maping &= ~(BIT(p_beamformee_entry->mu_reg_index));
			p_beamformee_entry->mu_reg_index = 0xFF;
		}
	}
}

/***********SU & MU BFee Entry Only when souding done****************/
void hal_txbf_8822b_status(
	void *dm_void,
	u8 beamform_idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u16 beam_ctrl_val, tmp_val;
	u32 beam_ctrl_reg;
	struct _RT_BEAMFORMING_INFO *beamforming_info = &dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY *beamform_entry;
	boolean is_mu_sounding = beamforming_info->is_mu_sounding, is_bitmap_ready = false;
	u16 bitmap;
	u8 idx, gid, i;
	u8 id1, id0;
	u32 gid_valid[6] = {0};
	u32 value32;
	boolean is_sounding_success[6] = {false};

	if (beamform_idx < BEAMFORMEE_ENTRY_NUM)
		beamform_entry = &beamforming_info->beamformee_entry[beamform_idx];
	else
		return;

	/*SU sounding done */
	if (is_mu_sounding == false) {
		if (phydm_acting_determine(dm, phydm_acting_as_ibss))
			beam_ctrl_val = beamform_entry->mac_id;
		else
			beam_ctrl_val = beamform_entry->p_aid;

		PHYDM_DBG(dm, DBG_TXBF,
			  "@%s, beamform_entry.beamform_entry_state = %d",
			  __func__, beamform_entry->beamform_entry_state);

		if (beamform_entry->su_reg_index == 0)
			beam_ctrl_reg = REG_TXBF_CTRL_8822B;
		else {
			beam_ctrl_reg = REG_TXBF_CTRL_8822B + 2;
			beam_ctrl_val |= BIT(12) | BIT(14) | BIT(15);
		}

		if (beamform_entry->beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			if (beamform_entry->sound_bw == CHANNEL_WIDTH_20)
				beam_ctrl_val |= BIT(9);
			else if (beamform_entry->sound_bw == CHANNEL_WIDTH_40)
				beam_ctrl_val |= (BIT(9) | BIT(10));
			else if (beamform_entry->sound_bw == CHANNEL_WIDTH_80)
				beam_ctrl_val |= (BIT(9) | BIT(10) | BIT(11));
		} else {
			PHYDM_DBG(dm, DBG_TXBF, "@%s, Don't apply Vmatrix",
				  __func__);
			beam_ctrl_val &= ~(BIT(9) | BIT(10) | BIT(11));
		}

		odm_write_2byte(dm, beam_ctrl_reg, beam_ctrl_val);
		/*@disable NDP packet use beamforming */
		tmp_val = odm_read_2byte(dm, REG_TXBF_CTRL_8822B);
		odm_write_2byte(dm, REG_TXBF_CTRL_8822B, tmp_val | BIT(15));
	} else {
		PHYDM_DBG(dm, DBG_TXBF, "@%s, MU Sounding Done\n", __func__);
		/*@MU sounding done */
		if (1) { /* @(beamform_entry->beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) { */
			PHYDM_DBG(dm, DBG_TXBF,
				  "@%s, BEAMFORMING_ENTRY_STATE_PROGRESSED\n",
				  __func__);

			value32 = odm_get_bb_reg(dm, R_0x1684, MASKDWORD);
			is_sounding_success[0] = (value32 & BIT(10)) ? 1 : 0;
			is_sounding_success[1] = (value32 & BIT(26)) ? 1 : 0;
			value32 = odm_get_bb_reg(dm, R_0x1688, MASKDWORD);
			is_sounding_success[2] = (value32 & BIT(10)) ? 1 : 0;
			is_sounding_success[3] = (value32 & BIT(26)) ? 1 : 0;
			value32 = odm_get_bb_reg(dm, R_0x168c, MASKDWORD);
			is_sounding_success[4] = (value32 & BIT(10)) ? 1 : 0;
			is_sounding_success[5] = (value32 & BIT(26)) ? 1 : 0;

			PHYDM_DBG(dm, DBG_TXBF,
				  "@%s, is_sounding_success STA1:%d,  STA2:%d, STA3:%d, STA4:%d, STA5:%d, STA6:%d\n",
				  __func__, is_sounding_success[0],
				  is_sounding_success[1],
				  is_sounding_success[2],
				  is_sounding_success[3],
				  is_sounding_success[4],
				  is_sounding_success[5]);

			value32 = odm_get_bb_reg(dm, R_0xf4c, 0xFFFF0000);
			/* odm_set_bb_reg(dm, R_0x19e0, MASKHWORD, 0xFFFF);Let MAC ignore bitmap */

			is_bitmap_ready = (boolean)((value32 & BIT(15)) >> 15);
			bitmap = (u16)(value32 & 0x3FFF);

			for (idx = 0; idx < 15; idx++) {
				if (idx < 5) { /*@bit0~4*/
					id0 = 0;
					id1 = (u8)(idx + 1);
				} else if (idx < 9) { /*@bit5~8*/
					id0 = 1;
					id1 = (u8)(idx - 3);
				} else if (idx < 12) { /*@bit9~11*/
					id0 = 2;
					id1 = (u8)(idx - 6);
				} else if (idx < 14) { /*@bit12~13*/
					id0 = 3;
					id1 = (u8)(idx - 8);
				} else { /*@bit14*/
					id0 = 4;
					id1 = (u8)(idx - 9);
				}
				if (bitmap & BIT(idx)) {
					/*Pair 1*/
					gid = (idx << 1) + 1;
					gid_valid[id0] |= (BIT(gid));
					gid_valid[id1] |= (BIT(gid));
					/*Pair 2*/
					gid += 1;
					gid_valid[id0] |= (BIT(gid));
					gid_valid[id1] |= (BIT(gid));
				} else {
					/*Pair 1*/
					gid = (idx << 1) + 1;
					gid_valid[id0] &= ~(BIT(gid));
					gid_valid[id1] &= ~(BIT(gid));
					/*Pair 2*/
					gid += 1;
					gid_valid[id0] &= ~(BIT(gid));
					gid_valid[id1] &= ~(BIT(gid));
				}
			}

			for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
				beamform_entry = &beamforming_info->beamformee_entry[i];
				if (beamform_entry->is_mu_sta && beamform_entry->mu_reg_index < 6) {
					value32 = gid_valid[beamform_entry->mu_reg_index];
					for (idx = 0; idx < 4; idx++) {
						beamform_entry->gid_valid[idx] = (u8)(value32 & 0xFF);
						value32 = (value32 >> 8);
					}
				}
			}

			for (idx = 0; idx < 6; idx++) {
				beamforming_info->reg_mu_tx_ctrl &= ~(BIT(8) | BIT(9) | BIT(10));
				beamforming_info->reg_mu_tx_ctrl |= ((idx << 8) & (BIT(8) | BIT(9) | BIT(10)));
				odm_write_4byte(dm, 0x14c0, beamforming_info->reg_mu_tx_ctrl);
				odm_set_mac_reg(dm, R_0x14c4, MASKDWORD, gid_valid[idx]); /*set MU STA gid valid table*/
			}

			/*@Enable TxMU PPDU*/
			if (beamforming_info->dbg_disable_mu_tx == false)
				beamforming_info->reg_mu_tx_ctrl |= BIT(7);
			else
				beamforming_info->reg_mu_tx_ctrl &= ~BIT(7);
			odm_write_4byte(dm, 0x14c0, beamforming_info->reg_mu_tx_ctrl);
		}
	}
}

/*Only used for MU BFer Entry when get GID management frame (self is as MU STA)*/
void hal_txbf_8822b_config_gtab(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _RT_BEAMFORMING_INFO *beamforming_info = &dm->beamforming_info;
	struct _RT_BEAMFORMER_ENTRY *beamformer_entry = NULL;
	u32 gid_valid = 0, user_position_l = 0, user_position_h = 0, i;

	if (beamforming_info->mu_ap_index < BEAMFORMER_ENTRY_NUM)
		beamformer_entry = &beamforming_info->beamformer_entry[beamforming_info->mu_ap_index];
	else
		return;

	PHYDM_DBG(dm, DBG_TXBF, "%s==>\n", __func__);

	/*@For GID 0~31*/
	for (i = 0; i < 4; i++)
		gid_valid |= (beamformer_entry->gid_valid[i] << (i << 3));
	for (i = 0; i < 8; i++) {
		if (i < 4)
			user_position_l |= (beamformer_entry->user_position[i] << (i << 3));
		else
			user_position_h |= (beamformer_entry->user_position[i] << ((i - 4) << 3));
	}
	/*select MU STA0 table*/
	beamforming_info->reg_mu_tx_ctrl &= ~(BIT(8) | BIT(9) | BIT(10));
	odm_write_4byte(dm, 0x14c0, beamforming_info->reg_mu_tx_ctrl);
	odm_set_bb_reg(dm, R_0x14c4, MASKDWORD, gid_valid);
	odm_set_bb_reg(dm, R_0x14c8, MASKDWORD, user_position_l);
	odm_set_bb_reg(dm, R_0x14cc, MASKDWORD, user_position_h);

	PHYDM_DBG(dm, DBG_TXBF,
		  "%s: STA0: gid_valid = 0x%x, user_position_l = 0x%x, user_position_h = 0x%x\n",
		  __func__, gid_valid, user_position_l, user_position_h);

	gid_valid = 0;
	user_position_l = 0;
	user_position_h = 0;

	/*@For GID 32~64*/
	for (i = 4; i < 8; i++)
		gid_valid |= (beamformer_entry->gid_valid[i] << ((i - 4) << 3));
	for (i = 8; i < 16; i++) {
		if (i < 4)
			user_position_l |= (beamformer_entry->user_position[i] << ((i - 8) << 3));
		else
			user_position_h |= (beamformer_entry->user_position[i] << ((i - 12) << 3));
	}
	/*select MU STA1 table*/
	beamforming_info->reg_mu_tx_ctrl &= ~(BIT(8) | BIT(9) | BIT(10));
	beamforming_info->reg_mu_tx_ctrl |= BIT(8);
	odm_write_4byte(dm, 0x14c0, beamforming_info->reg_mu_tx_ctrl);
	odm_set_bb_reg(dm, R_0x14c4, MASKDWORD, gid_valid);
	odm_set_bb_reg(dm, R_0x14c8, MASKDWORD, user_position_l);
	odm_set_bb_reg(dm, R_0x14cc, MASKDWORD, user_position_h);

	PHYDM_DBG(dm, DBG_TXBF,
		  "%s: STA1: gid_valid = 0x%x, user_position_l = 0x%x, user_position_h = 0x%x\n",
		  __func__, gid_valid, user_position_l, user_position_h);

	/* Set validity of MU STA0 and MU STA1*/
	beamforming_info->reg_mu_tx_ctrl &= 0xFFFFFFC0;
	beamforming_info->reg_mu_tx_ctrl |= 0x3; /* STA0, STA1*/
	odm_write_4byte(dm, 0x14c0, beamforming_info->reg_mu_tx_ctrl);
}

#if 0
/*This function translate the bitmap to GTAB*/
void
haltxbf8822b_gtab_translation(
	struct dm_struct			*dm
)
{
	u8 idx, gid;
	u8 id1, id0;
	u32 gid_valid[6] = {0};
	u32 user_position_lsb[6] = {0};
	u32 user_position_msb[6] = {0};

	for (idx = 0; idx < 15; idx++) {
		if (idx < 5) {/*@bit0~4*/
			id0 = 0;
			id1 = (u8)(idx + 1);
		} else if (idx < 9) { /*@bit5~8*/
			id0 = 1;
			id1 = (u8)(idx - 3);
		} else if (idx < 12) { /*@bit9~11*/
			id0 = 2;
			id1 = (u8)(idx - 6);
		} else if (idx < 14) { /*@bit12~13*/
			id0 = 3;
			id1 = (u8)(idx - 8);
		} else { /*@bit14*/
			id0 = 4;
			id1 = (u8)(idx - 9);
		}

		/*Pair 1*/
		gid = (idx << 1) + 1;
		gid_valid[id0] |= (1 << gid);
		gid_valid[id1] |= (1 << gid);
		if (gid < 16) {
			/*user_position_lsb[id0] |= (0 << (gid << 1));*/
			user_position_lsb[id1] |= (1 << (gid << 1));
		} else {
			/*user_position_msb[id0] |= (0 << ((gid - 16) << 1));*/
			user_position_msb[id1] |= (1 << ((gid - 16) << 1));
		}

		/*Pair 2*/
		gid += 1;
		gid_valid[id0] |= (1 << gid);
		gid_valid[id1] |= (1 << gid);
		if (gid < 16) {
			user_position_lsb[id0] |= (1 << (gid << 1));
			/*user_position_lsb[id1] |= (0 << (gid << 1));*/
		} else {
			user_position_msb[id0] |= (1 << ((gid - 16) << 1));
			/*user_position_msb[id1] |= (0 << ((gid - 16) << 1));*/
		}
	}


	for (idx = 0; idx < 6; idx++) {
		/*@dbg_print("gid_valid[%d] = 0x%x\n", idx, gid_valid[idx]);
		dbg_print("user_position[%d] = 0x%x   %x\n", idx, user_position_msb[idx], user_position_lsb[idx]);*/
	}
}
#endif

void hal_txbf_8822b_fw_txbf(
	void *dm_void,
	u8 idx)
{
#if 0
	struct _RT_BEAMFORMING_INFO	*beam_info = GET_BEAMFORM_INFO(adapter);
	struct _RT_BEAMFORMEE_ENTRY	*p_beam_entry = beam_info->beamformee_entry + idx;

	if (p_beam_entry->beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSING)
		hal_txbf_8822b_download_ndpa(adapter, idx);

	hal_txbf_8822b_fw_txbf_cmd(adapter);
#endif
}

#endif

#if (defined(CONFIG_BB_TXBF_API))
/*this function is only used for BFer*/
void phydm_8822btxbf_rfmode(void *dm_void, u8 su_bfee_cnt, u8 mu_bfee_cnt)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i;

	if (dm->rf_type == RF_1T1R)
		return;

	if (su_bfee_cnt > 0 || mu_bfee_cnt > 0) {
		for (i = RF_PATH_A; i <= RF_PATH_B; i++) {
			odm_set_rf_reg(dm, (enum rf_path)i, RF_0xef, BIT(19), 0x1); /*RF mode table write enable*/
			odm_set_rf_reg(dm, (enum rf_path)i, RF_0x33, 0xF, 3); /*Select RX mode*/
			odm_set_rf_reg(dm, (enum rf_path)i, RF_0x3e, 0xfffff, 0x00036); /*Set Table data*/
			odm_set_rf_reg(dm, (enum rf_path)i, RF_0x3f, 0xfffff, 0x5AFCE); /*Set Table data*/
			odm_set_rf_reg(dm, (enum rf_path)i, RF_0xef, BIT(19), 0x0); /*RF mode table write disable*/
		}
	}

	odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF1_8822B, BIT(30), 1); /*@if Nsts > Nc, don't apply V matrix*/

	if (su_bfee_cnt > 0 || mu_bfee_cnt > 0) {
		/*@for 8814 19ac(idx 1), 19b4(idx 0), different Tx ant setting*/
		odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF1_8822B, BIT(28) | BIT29, 0x2); /*@enable BB TxBF ant mapping register*/
		odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF1_8822B, BIT(31), 1); /*@ignore user since 8822B only 2Tx*/

		/*Nsts = 2	AB*/
		odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF1_8822B, 0xffff, 0x0433);
		odm_set_bb_reg(dm, REG_BB_TX_PATH_SEL_1_8822B, 0xfff00000, 0x043);

	} else {
		odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF1_8822B, BIT(28) | BIT29, 0x0); /*@enable BB TxBF ant mapping register*/
		odm_set_bb_reg(dm, REG_BB_TXBF_ANT_SET_BF1_8822B, BIT(31), 0); /*@ignore user since 8822B only 2Tx*/

		odm_set_bb_reg(dm, REG_BB_TX_PATH_SEL_1_8822B, 0xfff00000, 0x1); /*@1SS by path-A*/
		odm_set_bb_reg(dm, REG_BB_TX_PATH_SEL_2_8822B, MASKLWORD, 0x430); /*@2SS by path-A,B*/
	}
}

/*this function is for BFer bug workaround*/
void phydm_8822b_sutxbfer_workaroud(void *dm_void, boolean enable_su_bfer,
				    u8 nc, u8 nr, u8 ng, u8 CB, u8 BW,
				    boolean is_vht)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (enable_su_bfer) {
		odm_set_bb_reg(dm, R_0x19f8, BIT(22) | BIT(21) | BIT(20), 0x1);
		odm_set_bb_reg(dm, R_0x19f8, BIT(25) | BIT(24) | BIT(23), 0x0);
		odm_set_bb_reg(dm, R_0x19f8, BIT(16), 0x1);

		if (is_vht)
			odm_set_bb_reg(dm, R_0x19f0, BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0), 0x1f);
		else
			odm_set_bb_reg(dm, R_0x19f0, BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0), 0x22);

		odm_set_bb_reg(dm, R_0x19f0, BIT(7) | BIT(6), nc);
		odm_set_bb_reg(dm, R_0x19f0, BIT(9) | BIT(8), nr);
		odm_set_bb_reg(dm, R_0x19f0, BIT(11) | BIT(10), ng);
		odm_set_bb_reg(dm, R_0x19f0, BIT(13) | BIT(12), CB);

		odm_set_bb_reg(dm, R_0xb58, BIT(3) | BIT(2), BW);
		odm_set_bb_reg(dm, R_0xb58, BIT(7) | BIT(6) | BIT(5) | BIT(4), 0x0);
		odm_set_bb_reg(dm, R_0xb58, BIT(9) | BIT(8), BW);
		odm_set_bb_reg(dm, R_0xb58, BIT(13) | BIT(12) | BIT(11) | BIT(10), 0x0);
	} else {
		odm_set_bb_reg(dm, R_0x19f8, BIT(16), 0x0);
	}

	PHYDM_DBG(dm, DBG_TXBF, "[%s] enable_su_bfer = %d, is_vht = %d\n",
		  __func__, enable_su_bfer, is_vht);
	PHYDM_DBG(dm, DBG_TXBF,
		  "[%s] nc = %d, nr = %d, ng = %d, CB = %d, BW = %d\n",
		  __func__, nc, nr, ng, CB, BW);
}
#endif
#endif /* @(RTL8822B_SUPPORT == 1)*/
