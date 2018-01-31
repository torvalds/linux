/* SPDX-License-Identifier: GPL-2.0 */
/* ************************************************************
 * Description:
 *
 * This file is for 8192E TXBF mechanism
 *
 * ************************************************************ */
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (BEAMFORMING_SUPPORT == 1)
#if (RTL8192E_SUPPORT == 1)

void
hal_txbf_8192e_set_ndpa_rate(
	void			*p_dm_void,
	u8	BW,
	u8	rate
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_write_1byte(p_dm_odm, REG_NDPA_OPT_CTRL_8192E, (rate << 2 | BW));

}

void
hal_txbf_8192e_rf_mode(
	void			*p_dm_void,
	struct _RT_BEAMFORMING_INFO	*p_beam_info
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	bool				is_self_beamformer = false;
	bool				is_self_beamformee = false;
	enum beamforming_cap	beamform_cap = BEAMFORMING_CAP_NONE;

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (p_dm_odm->rf_type == ODM_1T1R)
		return;

	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_WE_LUT, 0x80000, 0x1); /*RF mode table write enable*/
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_B, RF_WE_LUT, 0x80000, 0x1); /*RF mode table write enable*/

	if (p_beam_info->beamformee_su_cnt > 0) {
		/*Path_A*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*Select RX mode  0x30=0x18000*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x31, 0xfffff, 0x0000f);	/*Set Table data*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x32, 0xfffff, 0x77fc2);	/*Enable TXIQGEN in RX mode*/
		/*Path_B*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_B, 0x30, 0xfffff, 0x18000);	/*Select RX mode*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_B, 0x31, 0xfffff, 0x0000f);	/*Set Table data*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_B, 0x32, 0xfffff, 0x77fc2);	/*Enable TXIQGEN in RX mode*/
	} else {
		/*Path_A*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*Select RX mode*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x31, 0xfffff, 0x0000f);	/*Set Table data*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x32, 0xfffff, 0x77f82);	/*Disable TXIQGEN in RX mode*/
		/*Path_B*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_B, 0x30, 0xfffff, 0x18000);	/*Select RX mode*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_B, 0x31, 0xfffff, 0x0000f);	/*Set Table data*/
		odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_B, 0x32, 0xfffff, 0x77f82);	/*Disable TXIQGEN in RX mode*/
	}

	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_WE_LUT, 0x80000, 0x0);	/*RF mode table write disable*/
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_B, RF_WE_LUT, 0x80000, 0x0);	/*RF mode table write disable*/

	if (p_beam_info->beamformee_su_cnt > 0) {
		odm_set_bb_reg(p_dm_odm, 0x90c, MASKDWORD, 0x83321333);
		odm_set_bb_reg(p_dm_odm, 0xa04, MASKBYTE3, 0xc1);
	} else
		odm_set_bb_reg(p_dm_odm, 0x90c, MASKDWORD, 0x81121313);
}



void
hal_txbf_8192e_fw_txbf_cmd(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	idx, period0 = 0, period1 = 0;
	u8	PageNum0 = 0xFF, PageNum1 = 0xFF;
	u8	u1_tx_bf_parm[3] = {0};
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm_odm->beamforming_info;

	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		if (p_beam_info->beamformee_entry[idx].beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			if (idx == 0) {
				if (p_beam_info->beamformee_entry[idx].is_sound)
					PageNum0 = 0xFE;
				else
					PageNum0 = 0xFF; /* stop sounding */
				period0 = (u8)(p_beam_info->beamformee_entry[idx].sound_period);
			} else if (idx == 1) {
				if (p_beam_info->beamformee_entry[idx].is_sound)
					PageNum1 = 0xFE;
				else
					PageNum1 = 0xFF; /* stop sounding */
				period1 = (u8)(p_beam_info->beamformee_entry[idx].sound_period);
			}
		}
	}

	u1_tx_bf_parm[0] = PageNum0;
	u1_tx_bf_parm[1] = PageNum1;
	u1_tx_bf_parm[2] = (period1 << 4) | period0;
	odm_fill_h2c_cmd(p_dm_odm, PHYDM_H2C_TXBF, 3, u1_tx_bf_parm);

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD,
		("[%s] PageNum0 = %d period0 = %d, PageNum1 = %d period1 %d\n", __func__, PageNum0, period0, PageNum1, period1));
}


void
hal_txbf_8192e_download_ndpa(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			u1b_tmp = 0, tmp_reg422 = 0, head_page;
	u8			bcn_valid_reg = 0, count = 0, dl_bcn_count = 0;
	bool			is_send_beacon = false;
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	u8			tx_page_bndy = LAST_ENTRY_OF_TX_PKT_BUFFER_8812;
	/*default reseved 1 page for the IC type which is undefined.*/
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm_odm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	*p_beam_entry = p_beam_info->beamformee_entry + idx;

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	*p_dm_odm->p_is_fw_dw_rsvd_page_in_progress = true;
#endif
	if (idx == 0)
		head_page = 0xFE;
	else
		head_page = 0xFE;

	phydm_get_hal_def_var_handler_interface(p_dm_odm, HAL_DEF_TX_PAGE_BOUNDARY, (u8 *)&tx_page_bndy);

	/*Set REG_CR bit 8. DMA beacon by SW.*/
	u1b_tmp = odm_read_1byte(p_dm_odm, REG_CR_8192E+1);
	odm_write_1byte(p_dm_odm,  REG_CR_8192E+1, (u1b_tmp | BIT(0)));

	/*Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.*/
	tmp_reg422 = odm_read_1byte(p_dm_odm, REG_FWHW_TXQ_CTRL_8192E+2);
	odm_write_1byte(p_dm_odm, REG_FWHW_TXQ_CTRL_8192E+2,  tmp_reg422 & (~BIT(6)));

	if (tmp_reg422 & BIT(6)) {
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_WARNING, ("%s There is an adapter is sending beacon.\n", __func__));
		is_send_beacon = true;
	}

	/*TDECTRL[15:8] 0x209[7:0] = 0xFE/0xFD	NDPA Head for TXDMA*/
	odm_write_1byte(p_dm_odm, REG_DWBCN0_CTRL_8192E+1, head_page);

	do {
		/*Clear beacon valid check bit.*/
		bcn_valid_reg = odm_read_1byte(p_dm_odm, REG_DWBCN0_CTRL_8192E+2);
		odm_write_1byte(p_dm_odm, REG_DWBCN0_CTRL_8192E+2, (bcn_valid_reg | BIT(0)));

		/* download NDPA rsvd page. */
		beamforming_send_ht_ndpa_packet(p_dm_odm, p_beam_entry->mac_addr, p_beam_entry->sound_bw, BEACON_QUEUE);

#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)
		u1b_tmp = odm_read_1byte(p_dm_odm, REG_MGQ_TXBD_NUM_8192E+3);
		count = 0;
		while ((count < 20) && (u1b_tmp & BIT(4))) {
			count++;
			ODM_delay_us(10);
			u1b_tmp = odm_read_1byte(p_dm_odm, REG_MGQ_TXBD_NUM_8192E+3);
		}
		odm_write_1byte(p_dm_odm, REG_MGQ_TXBD_NUM_8192E+3, u1b_tmp | BIT(4));
#endif

		/*check rsvd page download OK.*/
		bcn_valid_reg = odm_read_1byte(p_dm_odm, REG_DWBCN0_CTRL_8192E+2);
		count = 0;
		while (!(bcn_valid_reg & BIT(0)) && count < 20) {
			count++;
			ODM_delay_us(10);
			bcn_valid_reg = odm_read_1byte(p_dm_odm, REG_DWBCN0_CTRL_8192E+2);
		}
		dl_bcn_count++;
	} while (!(bcn_valid_reg & BIT(0)) && dl_bcn_count < 5);

	if (!(bcn_valid_reg & BIT(0)))
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_WARNING, ("%s Download RSVD page failed!\n", __func__));

	/*TDECTRL[15:8] 0x209[7:0] = 0xF9	Beacon Head for TXDMA*/
	odm_write_1byte(p_dm_odm, REG_DWBCN0_CTRL_8192E+1, tx_page_bndy);

	/*To make sure that if there exists an adapter which would like to send beacon.*/
	/*If exists, the origianl value of 0x422[6] will be 1, we should check this to*/
	/*prevent from setting 0x422[6] to 0 after download reserved page, or it will cause*/
	/*the beacon cannot be sent by HW.*/
	/*2010.06.23. Added by tynli.*/
	if (is_send_beacon)
		odm_write_1byte(p_dm_odm, REG_FWHW_TXQ_CTRL_8192E+2, tmp_reg422);

	/*Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli.*/
	/*Clear CR[8] or beacon packet will not be send to TxBuf anymore.*/
	u1b_tmp = odm_read_1byte(p_dm_odm, REG_CR_8192E+1);
	odm_write_1byte(p_dm_odm, REG_CR_8192E+1, (u1b_tmp & (~BIT(0))));

	p_beam_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSED;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	*p_dm_odm->p_is_fw_dw_rsvd_page_in_progress = false;
#endif
}


void
hal_txbf_8192e_enter(
	void			*p_dm_void,
	u8				bfer_bfee_idx
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8					i = 0;
	u8					bfer_idx = (bfer_bfee_idx & 0xF0) >> 4;
	u8					bfee_idx = (bfer_bfee_idx & 0xF);
	u32					csi_param;
	struct _RT_BEAMFORMING_INFO	*p_beamforming_info = &p_dm_odm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	beamformee_entry;
	struct _RT_BEAMFORMER_ENTRY	beamformer_entry;
	u16					sta_id = 0;

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	hal_txbf_8192e_rf_mode(p_dm_odm, p_beamforming_info);

	if (p_dm_odm->rf_type == ODM_2T2R)
		odm_write_4byte(p_dm_odm, 0xd80, 0x00000000);		/*nc =2*/

	if ((p_beamforming_info->beamformer_su_cnt > 0) && (bfer_idx < BEAMFORMER_ENTRY_NUM)) {
		beamformer_entry = p_beamforming_info->beamformer_entry[bfer_idx];

		/*Sounding protocol control*/
		odm_write_1byte(p_dm_odm, REG_SND_PTCL_CTRL_8192E, 0xCB);

		/*MAC address/Partial AID of Beamformer*/
		if (bfer_idx == 0) {
			for (i = 0; i < 6 ; i++)
				odm_write_1byte(p_dm_odm, (REG_ASSOCIATED_BFMER0_INFO_8192E+i), beamformer_entry.mac_addr[i]);
		} else {
			for (i = 0; i < 6 ; i++)
				odm_write_1byte(p_dm_odm, (REG_ASSOCIATED_BFMER1_INFO_8192E+i), beamformer_entry.mac_addr[i]);
		}

		/*CSI report parameters of Beamformer Default use nc = 2*/
		csi_param = 0x03090309;

		odm_write_4byte(p_dm_odm, REG_CSI_RPT_PARAM_BW20_8192E, csi_param);
		odm_write_4byte(p_dm_odm, REG_CSI_RPT_PARAM_BW40_8192E, csi_param);
		odm_write_4byte(p_dm_odm, REG_CSI_RPT_PARAM_BW80_8192E, csi_param);

		/*Timeout value for MAC to leave NDP_RX_standby_state (60 us, Test chip) (80 us,  MP chip)*/
		odm_write_1byte(p_dm_odm, REG_SND_PTCL_CTRL_8192E+3, 0x50);

	}

	if ((p_beamforming_info->beamformee_su_cnt > 0) && (bfee_idx < BEAMFORMEE_ENTRY_NUM)) {
		beamformee_entry = p_beamforming_info->beamformee_entry[bfee_idx];

		if (phydm_acting_determine(p_dm_odm, phydm_acting_as_ibss))
			sta_id = beamformee_entry.mac_id;
		else
			sta_id = beamformee_entry.p_aid;

		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s], sta_id=0x%X\n", __func__, sta_id));

		/*P_AID of Beamformee & enable NDPA transmission & enable NDPA interrupt*/
		if (bfee_idx == 0) {
			odm_write_2byte(p_dm_odm, REG_TXBF_CTRL_8192E, sta_id);
			odm_write_1byte(p_dm_odm, REG_TXBF_CTRL_8192E+3, odm_read_1byte(p_dm_odm, REG_TXBF_CTRL_8192E+3) | BIT(4) | BIT(6) | BIT(7));
		} else
			odm_write_2byte(p_dm_odm, REG_TXBF_CTRL_8192E+2, sta_id | BIT(12) | BIT(14) | BIT(15));

		/*CSI report parameters of Beamformee*/
		if (bfee_idx == 0) {
			/*Get BIT24 & BIT25*/
			u8 tmp = odm_read_1byte(p_dm_odm, REG_ASSOCIATED_BFMEE_SEL_8192E+3) & 0x3;

			odm_write_1byte(p_dm_odm, REG_ASSOCIATED_BFMEE_SEL_8192E+3, tmp | 0x60);
			odm_write_2byte(p_dm_odm, REG_ASSOCIATED_BFMEE_SEL_8192E, sta_id | BIT(9));
		} else {
			/*Set BIT25*/
			odm_write_2byte(p_dm_odm, REG_ASSOCIATED_BFMEE_SEL_8192E+2, sta_id | 0xE200);
		}
		phydm_beamforming_notify(p_dm_odm);

	}
}


void
hal_txbf_8192e_leave(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm_odm->beamforming_info;

	hal_txbf_8192e_rf_mode(p_dm_odm, p_beam_info);

	/*	Clear P_AID of Beamformee
	*	Clear MAC addresss of Beamformer
	*	Clear Associated Bfmee Sel
	*/
	if (p_beam_info->beamform_cap == BEAMFORMING_CAP_NONE)
		odm_write_1byte(p_dm_odm, REG_SND_PTCL_CTRL_8192E, 0xC8);

	if (idx == 0) {
		odm_write_2byte(p_dm_odm, REG_TXBF_CTRL_8192E, 0);
		odm_write_4byte(p_dm_odm, REG_ASSOCIATED_BFMER0_INFO_8192E, 0);
		odm_write_2byte(p_dm_odm, REG_ASSOCIATED_BFMER0_INFO_8192E+4, 0);
		odm_write_2byte(p_dm_odm, REG_ASSOCIATED_BFMEE_SEL_8192E, 0);
	} else {
		odm_write_2byte(p_dm_odm, REG_TXBF_CTRL_8192E+2, odm_read_1byte(p_dm_odm, REG_TXBF_CTRL_8192E+2) & 0xF000);
		odm_write_4byte(p_dm_odm, REG_ASSOCIATED_BFMER1_INFO_8192E, 0);
		odm_write_2byte(p_dm_odm, REG_ASSOCIATED_BFMER1_INFO_8192E+4, 0);
		odm_write_2byte(p_dm_odm, REG_ASSOCIATED_BFMEE_SEL_8192E+2, odm_read_2byte(p_dm_odm, REG_ASSOCIATED_BFMEE_SEL_8192E+2) & 0x60);
	}

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] idx %d\n", __func__, idx));
}


void
hal_txbf_8192e_status(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u16					beam_ctrl_val;
	u32					beam_ctrl_reg;
	struct _RT_BEAMFORMING_INFO	*p_beam_info =  &p_dm_odm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	beamform_entry = p_beam_info->beamformee_entry[idx];

	if (phydm_acting_determine(p_dm_odm, phydm_acting_as_ibss))
		beam_ctrl_val = beamform_entry.mac_id;
	else
		beam_ctrl_val = beamform_entry.p_aid;

	if (idx == 0)
		beam_ctrl_reg = REG_TXBF_CTRL_8192E;
	else {
		beam_ctrl_reg = REG_TXBF_CTRL_8192E+2;
		beam_ctrl_val |= BIT(12) | BIT(14) | BIT(15);
	}

	if ((beamform_entry.beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) && (p_beam_info->apply_v_matrix == true)) {
		if (beamform_entry.sound_bw == CHANNEL_WIDTH_20)
			beam_ctrl_val |= BIT(9);
		else if (beamform_entry.sound_bw == CHANNEL_WIDTH_40)
			beam_ctrl_val |= BIT(10);
	} else
		beam_ctrl_val &= ~(BIT(9) | BIT(10) | BIT(11));

	odm_write_2byte(p_dm_odm, beam_ctrl_reg, beam_ctrl_val);

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] idx %d beam_ctrl_reg %x beam_ctrl_val %x\n", __func__, idx, beam_ctrl_reg, beam_ctrl_val));
}


void
hal_txbf_8192e_fw_tx_bf(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm_odm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	*p_beam_entry = p_beam_info->beamformee_entry + idx;

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (p_beam_entry->beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSING)
		hal_txbf_8192e_download_ndpa(p_dm_odm, idx);

	hal_txbf_8192e_fw_txbf_cmd(p_dm_odm);
}

#endif	/* #if (RTL8192E_SUPPORT == 1)*/

#endif
