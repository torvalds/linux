/* SPDX-License-Identifier: GPL-2.0 */
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
/* ************************************************************
 * Description:
 *
 * This file is for 8812/8821/8811 TXBF mechanism
 *
 * ************************************************************ */
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (BEAMFORMING_SUPPORT == 1)
#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
void
hal_txbf_8812a_set_ndpa_rate(
	void			*p_dm_void,
	u8	BW,
	u8	rate
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_write_1byte(p_dm, REG_NDPA_OPT_CTRL_8812A, (rate << 2 | BW));

}

void
hal_txbf_jaguar_rf_mode(
	void			*p_dm_void,
	struct _RT_BEAMFORMING_INFO	*p_beam_info
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->rf_type == RF_1T1R)
		return;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] set TxIQGen\n", __func__));

	odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);	/*RF mode table write enable*/
	odm_set_rf_reg(p_dm, RF_PATH_B, 0xef, 0x80000, 0x1);	/*RF mode table write enable*/

	if (p_beam_info->beamformee_su_cnt > 0) {
		/* Paath_A */
		odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0x78000, 0x3);		/*Select RX mode*/
		odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x3F7FF);	/*Set Table data*/
		odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0xE26BF);	/*Enable TXIQGEN in RX mode*/
		/* Path_B */
		odm_set_rf_reg(p_dm, RF_PATH_B, 0x30, 0x78000, 0x3);		/*Select RX mode*/
		odm_set_rf_reg(p_dm, RF_PATH_B, 0x31, 0xfffff, 0x3F7FF);	/*Set Table data*/
		odm_set_rf_reg(p_dm, RF_PATH_B, 0x32, 0xfffff, 0xE26BF);	/*Enable TXIQGEN in RX mode*/
	} else {
		/* Paath_A */
		odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0x78000, 0x3);		/*Select RX mode*/
		odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x3F7FF);	/*Set Table data*/
		odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0xC26BF);	/*Disable TXIQGEN in RX mode*/
		/* Path_B */
		odm_set_rf_reg(p_dm, RF_PATH_B, 0x30, 0x78000, 0x3);		/*Select RX mode*/
		odm_set_rf_reg(p_dm, RF_PATH_B, 0x31, 0xfffff, 0x3F7FF);	/*Set Table data*/
		odm_set_rf_reg(p_dm, RF_PATH_B, 0x32, 0xfffff, 0xC26BF);	/*Disable TXIQGEN in RX mode*/
	}

	odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);	/*RF mode table write disable*/
	odm_set_rf_reg(p_dm, RF_PATH_B, 0xef, 0x80000, 0x0);	/*RF mode table write disable*/

	if (p_beam_info->beamformee_su_cnt > 0)
		odm_set_bb_reg(p_dm, 0x80c, MASKBYTE1, 0x33);
	else
		odm_set_bb_reg(p_dm, 0x80c, MASKBYTE1, 0x11);
}


void
hal_txbf_jaguar_download_ndpa(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			u1b_tmp = 0, tmp_reg422 = 0, head_page;
	u8			bcn_valid_reg = 0, count = 0, dl_bcn_count = 0;
	boolean			is_send_beacon = false;
	u8			tx_page_bndy = LAST_ENTRY_OF_TX_PKT_BUFFER_8812;	/*default reseved 1 page for the IC type which is undefined.*/
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	*p_beam_entry = p_beam_info->beamformee_entry + idx;
	struct _ADAPTER		*adapter = p_dm->adapter;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	*p_dm->p_is_fw_dw_rsvd_page_in_progress = true;
#endif
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	if (idx == 0)
		head_page = 0xFE;
	else
		head_page = 0xFE;

	phydm_get_hal_def_var_handler_interface(p_dm, HAL_DEF_TX_PAGE_BOUNDARY, (u8 *)&tx_page_bndy);

	/*Set REG_CR bit 8. DMA beacon by SW.*/
	u1b_tmp = odm_read_1byte(p_dm, REG_CR_8812A + 1);
	odm_write_1byte(p_dm,  REG_CR_8812A + 1, (u1b_tmp | BIT(0)));


	/*Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.*/
	tmp_reg422 = odm_read_1byte(p_dm, REG_FWHW_TXQ_CTRL_8812A + 2);
	odm_write_1byte(p_dm, REG_FWHW_TXQ_CTRL_8812A + 2,  tmp_reg422 & (~BIT(6)));

	if (tmp_reg422 & BIT(6)) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("SetBeamformDownloadNDPA_8812(): There is an adapter is sending beacon.\n"));
		is_send_beacon = true;
	}

	/*TDECTRL[15:8] 0x209[7:0] = 0xF6	Beacon Head for TXDMA*/
	odm_write_1byte(p_dm, REG_TDECTRL_8812A + 1, head_page);

	do {
		/*Clear beacon valid check bit.*/
		bcn_valid_reg = odm_read_1byte(p_dm, REG_TDECTRL_8812A + 2);
		odm_write_1byte(p_dm, REG_TDECTRL_8812A + 2, (bcn_valid_reg | BIT(0)));

		/*download NDPA rsvd page.*/
		if (p_beam_entry->beamform_entry_cap & BEAMFORMER_CAP_VHT_SU)
			beamforming_send_vht_ndpa_packet(p_dm, p_beam_entry->mac_addr, p_beam_entry->aid, p_beam_entry->sound_bw, BEACON_QUEUE);
		else
			beamforming_send_ht_ndpa_packet(p_dm, p_beam_entry->mac_addr, p_beam_entry->sound_bw, BEACON_QUEUE);

		/*check rsvd page download OK.*/
		bcn_valid_reg = odm_read_1byte(p_dm, REG_TDECTRL_8812A + 2);
		count = 0;
		while (!(bcn_valid_reg & BIT(0)) && count < 20) {
			count++;
			ODM_delay_ms(10);
			bcn_valid_reg = odm_read_1byte(p_dm, REG_TDECTRL_8812A + 2);
		}
		dl_bcn_count++;
	} while (!(bcn_valid_reg & BIT(0)) && dl_bcn_count < 5);

	if (!(bcn_valid_reg & BIT(0)))
		PHYDM_DBG(p_dm, DBG_TXBF, ("%s Download RSVD page failed!\n", __func__));

	/*TDECTRL[15:8] 0x209[7:0] = 0xF6	Beacon Head for TXDMA*/
	odm_write_1byte(p_dm, REG_TDECTRL_8812A + 1, tx_page_bndy);

	/*To make sure that if there exists an adapter which would like to send beacon.*/
	/*If exists, the origianl value of 0x422[6] will be 1, we should check this to*/
	/*prevent from setting 0x422[6] to 0 after download reserved page, or it will cause*/
	/*the beacon cannot be sent by HW.*/
	/*2010.06.23. Added by tynli.*/
	if (is_send_beacon)
		odm_write_1byte(p_dm, REG_FWHW_TXQ_CTRL_8812A + 2, tmp_reg422);

	/*Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli.*/
	/*Clear CR[8] or beacon packet will not be send to TxBuf anymore.*/
	u1b_tmp = odm_read_1byte(p_dm, REG_CR_8812A + 1);
	odm_write_1byte(p_dm, REG_CR_8812A + 1, (u1b_tmp & (~BIT(0))));

	p_beam_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSED;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	*p_dm->p_is_fw_dw_rsvd_page_in_progress = false;
#endif
}


void
hal_txbf_jaguar_fw_txbf_cmd(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	idx, period0 = 0, period1 = 0;
	u8	PageNum0 = 0xFF, PageNum1 = 0xFF;
	u8	u1_tx_bf_parm[3] = {0};
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm->beamforming_info;

	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		/*Modified by David*/
		if (p_beam_info->beamformee_entry[idx].is_used && p_beam_info->beamformee_entry[idx].beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			if (idx == 0) {
				if (p_beam_info->beamformee_entry[idx].is_sound)
					PageNum0 = 0xFE;
				else
					PageNum0 = 0xFF; /*stop sounding*/
				period0 = (u8)(p_beam_info->beamformee_entry[idx].sound_period);
			} else if (idx == 1) {
				if (p_beam_info->beamformee_entry[idx].is_sound)
					PageNum1 = 0xFE;
				else
					PageNum1 = 0xFF; /*stop sounding*/
				period1 = (u8)(p_beam_info->beamformee_entry[idx].sound_period);
			}
		}
	}

	u1_tx_bf_parm[0] = PageNum0;
	u1_tx_bf_parm[1] = PageNum1;
	u1_tx_bf_parm[2] = (period1 << 4) | period0;
	odm_fill_h2c_cmd(p_dm, PHYDM_H2C_TXBF, 3, u1_tx_bf_parm);

	PHYDM_DBG(p_dm, DBG_TXBF,
		("[%s] PageNum0 = %d period0 = %d, PageNum1 = %d period1 %d\n", __func__, PageNum0, period0, PageNum1, period1));
}


void
hal_txbf_jaguar_enter(
	void			*p_dm_void,
	u8				bfer_bfee_idx
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8					i = 0;
	u8					bfer_idx = (bfer_bfee_idx & 0xF0) >> 4;
	u8					bfee_idx = (bfer_bfee_idx & 0xF);
	u32					csi_param;
	struct _RT_BEAMFORMING_INFO	*p_beamforming_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	beamformee_entry;
	struct _RT_BEAMFORMER_ENTRY	beamformer_entry;
	u16					sta_id = 0;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s]Start!\n", __func__));

	hal_txbf_jaguar_rf_mode(p_dm, p_beamforming_info);

	if (p_dm->rf_type == RF_2T2R)
		odm_set_bb_reg(p_dm, ODM_REG_CSI_CONTENT_VALUE, MASKDWORD, 0x00000000);	/*nc =2*/
	else
		odm_set_bb_reg(p_dm, ODM_REG_CSI_CONTENT_VALUE, MASKDWORD, 0x01081008);	/*nc =1*/

	if ((p_beamforming_info->beamformer_su_cnt > 0) && (bfer_idx < BEAMFORMER_ENTRY_NUM)) {
		beamformer_entry = p_beamforming_info->beamformer_entry[bfer_idx];

		/*Sounding protocol control*/
		odm_write_1byte(p_dm, REG_SND_PTCL_CTRL_8812A, 0xCB);

		/*MAC address/Partial AID of Beamformer*/
		if (bfer_idx == 0) {
			for (i = 0; i < 6 ; i++)
				odm_write_1byte(p_dm, (REG_BFMER0_INFO_8812A + i), beamformer_entry.mac_addr[i]);
			/*CSI report use legacy ofdm so don't need to fill P_AID. */
			/*platform_efio_write_2byte(adapter, REG_BFMER0_INFO_8812A+6, beamform_entry.P_AID); */
		} else {
			for (i = 0; i < 6 ; i++)
				odm_write_1byte(p_dm, (REG_BFMER1_INFO_8812A + i), beamformer_entry.mac_addr[i]);
			/*CSI report use legacy ofdm so don't need to fill P_AID.*/
			/*platform_efio_write_2byte(adapter, REG_BFMER1_INFO_8812A+6, beamform_entry.P_AID);*/
		}

		/*CSI report parameters of Beamformee*/
		if (beamformer_entry.beamform_entry_cap & BEAMFORMEE_CAP_VHT_SU) {
			if (p_dm->rf_type == RF_2T2R)
				csi_param = 0x01090109;
			else
				csi_param = 0x01080108;
		} else {
			if (p_dm->rf_type == RF_2T2R)
				csi_param = 0x03090309;
			else
				csi_param = 0x03080308;
		}

		odm_write_4byte(p_dm, REG_CSI_RPT_PARAM_BW20_8812A, csi_param);
		odm_write_4byte(p_dm, REG_CSI_RPT_PARAM_BW40_8812A, csi_param);
		odm_write_4byte(p_dm, REG_CSI_RPT_PARAM_BW80_8812A, csi_param);

		/*Timeout value for MAC to leave NDP_RX_standby_state (60 us, Test chip) (80 us,  MP chip)*/
		odm_write_1byte(p_dm, REG_SND_PTCL_CTRL_8812A + 3, 0x50);
	}


	if ((p_beamforming_info->beamformee_su_cnt > 0) && (bfee_idx < BEAMFORMEE_ENTRY_NUM)) {
		beamformee_entry = p_beamforming_info->beamformee_entry[bfee_idx];

		if (phydm_acting_determine(p_dm, phydm_acting_as_ibss))
			sta_id = beamformee_entry.mac_id;
		else
			sta_id = beamformee_entry.p_aid;

		/*P_AID of Beamformee & enable NDPA transmission & enable NDPA interrupt*/
		if (bfee_idx == 0) {
			odm_write_2byte(p_dm, REG_TXBF_CTRL_8812A, sta_id);
			odm_write_1byte(p_dm, REG_TXBF_CTRL_8812A + 3, odm_read_1byte(p_dm, REG_TXBF_CTRL_8812A + 3) | BIT(4) | BIT(6) | BIT(7));
		} else
			odm_write_2byte(p_dm, REG_TXBF_CTRL_8812A + 2, sta_id | BIT(12) | BIT(14) | BIT(15));

		/*CSI report parameters of Beamformee*/
		if (bfee_idx == 0) {
			/*Get BIT24 & BIT25*/
			u8	tmp = odm_read_1byte(p_dm, REG_BFMEE_SEL_8812A + 3) & 0x3;

			odm_write_1byte(p_dm, REG_BFMEE_SEL_8812A + 3, tmp | 0x60);
			odm_write_2byte(p_dm, REG_BFMEE_SEL_8812A, sta_id | BIT(9));
		} else {
			/*Set BIT25*/
			odm_write_2byte(p_dm, REG_BFMEE_SEL_8812A + 2, sta_id | 0xE200);
		}
		phydm_beamforming_notify(p_dm);
	}
}


void
hal_txbf_jaguar_leave(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_BEAMFORMING_INFO	*p_beamforming_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMER_ENTRY	beamformer_entry;
	struct _RT_BEAMFORMEE_ENTRY	beamformee_entry;

	if (idx < BEAMFORMER_ENTRY_NUM) {
		beamformer_entry = p_beamforming_info->beamformer_entry[idx];
		beamformee_entry = p_beamforming_info->beamformee_entry[idx];
	} else
		return;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s]Start!, IDx = %d\n", __func__, idx));

	/*Clear P_AID of Beamformee*/
	/*Clear MAC address of Beamformer*/
	/*Clear Associated Bfmee Sel*/

	if (beamformer_entry.beamform_entry_cap == BEAMFORMING_CAP_NONE) {
		odm_write_1byte(p_dm, REG_SND_PTCL_CTRL_8812A, 0xC8);
		if (idx == 0) {
			odm_write_4byte(p_dm, REG_BFMER0_INFO_8812A, 0);
			odm_write_2byte(p_dm, REG_BFMER0_INFO_8812A + 4, 0);
			odm_write_2byte(p_dm, REG_CSI_RPT_PARAM_BW20_8812A, 0);
			odm_write_2byte(p_dm, REG_CSI_RPT_PARAM_BW40_8812A, 0);
			odm_write_2byte(p_dm, REG_CSI_RPT_PARAM_BW80_8812A, 0);
		} else {
			odm_write_4byte(p_dm, REG_BFMER1_INFO_8812A, 0);
			odm_write_2byte(p_dm, REG_BFMER1_INFO_8812A + 4, 0);
			odm_write_2byte(p_dm, REG_CSI_RPT_PARAM_BW20_8812A, 0);
			odm_write_2byte(p_dm, REG_CSI_RPT_PARAM_BW40_8812A, 0);
			odm_write_2byte(p_dm, REG_CSI_RPT_PARAM_BW80_8812A, 0);
		}
	}

	if (beamformee_entry.beamform_entry_cap == BEAMFORMING_CAP_NONE) {
		hal_txbf_jaguar_rf_mode(p_dm, p_beamforming_info);
		if (idx == 0) {
			odm_write_2byte(p_dm, REG_TXBF_CTRL_8812A, 0x0);
			odm_write_2byte(p_dm, REG_BFMEE_SEL_8812A, 0);
		} else {
			odm_write_2byte(p_dm, REG_TXBF_CTRL_8812A + 2, odm_read_2byte(p_dm, REG_TXBF_CTRL_8812A + 2) & 0xF000);
			odm_write_2byte(p_dm, REG_BFMEE_SEL_8812A + 2, odm_read_2byte(p_dm, REG_BFMEE_SEL_8812A + 2) & 0x60);
		}
	}

}


void
hal_txbf_jaguar_status(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u16					beam_ctrl_val;
	u32					beam_ctrl_reg;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	beamform_entry = p_beam_info->beamformee_entry[idx];

	if (phydm_acting_determine(p_dm, phydm_acting_as_ibss))
		beam_ctrl_val = beamform_entry.mac_id;
	else
		beam_ctrl_val = beamform_entry.p_aid;

	if (idx == 0)
		beam_ctrl_reg = REG_TXBF_CTRL_8812A;
	else {
		beam_ctrl_reg = REG_TXBF_CTRL_8812A + 2;
		beam_ctrl_val |= BIT(12) | BIT(14) | BIT(15);
	}

	if ((beamform_entry.beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) && (p_beam_info->apply_v_matrix == true)) {
		if (beamform_entry.sound_bw == CHANNEL_WIDTH_20)
			beam_ctrl_val |= BIT(9);
		else if (beamform_entry.sound_bw == CHANNEL_WIDTH_40)
			beam_ctrl_val |= (BIT(9) | BIT(10));
		else if (beamform_entry.sound_bw == CHANNEL_WIDTH_80)
			beam_ctrl_val |= (BIT(9) | BIT(10) | BIT(11));
	} else
		beam_ctrl_val &= ~(BIT(9) | BIT(10) | BIT(11));

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] beam_ctrl_val = 0x%x!\n", __func__, beam_ctrl_val));

	odm_write_2byte(p_dm, beam_ctrl_reg, beam_ctrl_val);
}



void
hal_txbf_jaguar_fw_txbf(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	*p_beam_entry = p_beam_info->beamformee_entry + idx;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	if (p_beam_entry->beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSING)
		hal_txbf_jaguar_download_ndpa(p_dm, idx);

	hal_txbf_jaguar_fw_txbf_cmd(p_dm);
}


void
hal_txbf_jaguar_patch(
	void			*p_dm_void,
	u8				operation
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm->beamforming_info;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	if (p_beam_info->beamform_cap == BEAMFORMING_CAP_NONE)
		return;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (operation == SCAN_OPT_BACKUP_BAND0)
		odm_write_1byte(p_dm, REG_SND_PTCL_CTRL_8812A, 0xC8);
	else if (operation == SCAN_OPT_RESTORE)
		odm_write_1byte(p_dm, REG_SND_PTCL_CTRL_8812A, 0xCB);
#endif
}

void
hal_txbf_jaguar_clk_8812a(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u16	u2btmp;
	u8	count = 0, u1btmp;
	struct _ADAPTER	*adapter = p_dm->adapter;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	if (*(p_dm->p_is_scan_in_process)) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] return by Scan\n", __func__));
		return;
	}
#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	/*Stop PCIe TxDMA*/
	odm_write_1byte(p_dm, REG_PCIE_CTRL_REG_8812A + 1, 0xFE);
#endif

	/*Stop Usb TxDMA*/
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	RT_DISABLE_FUNC(adapter, DF_TX_BIT);
	PlatformReturnAllPendingTxPackets(adapter);
#else
	rtw_write_port_cancel(adapter);
#endif

	/*Wait TXFF empty*/
	for (count = 0; count < 100; count++) {
		u2btmp = odm_read_2byte(p_dm, REG_TXPKT_EMPTY_8812A);
		u2btmp = u2btmp & 0xfff;
		if (u2btmp != 0xfff) {
			ODM_delay_ms(10);
			continue;
		} else
			break;
	}

	/*TX pause*/
	odm_write_1byte(p_dm, REG_TXPAUSE_8812A, 0xFF);

	/*Wait TX state Machine OK*/
	for (count = 0; count < 100; count++) {
		if (odm_read_4byte(p_dm, REG_SCH_TXCMD_8812A) != 0)
			continue;
		else
			break;
	}


	/*Stop RX DMA path*/
	u1btmp = odm_read_1byte(p_dm, REG_RXDMA_CONTROL_8812A);
	odm_write_1byte(p_dm, REG_RXDMA_CONTROL_8812A, u1btmp | BIT(2));

	for (count = 0; count < 100; count++) {
		u1btmp = odm_read_1byte(p_dm, REG_RXDMA_CONTROL_8812A);
		if (u1btmp & BIT(1))
			break;
		else
			ODM_delay_ms(10);
	}

	/*Disable clock*/
	odm_write_1byte(p_dm, REG_SYS_CLKR_8812A + 1, 0xf0);
	/*Disable 320M*/
	odm_write_1byte(p_dm, REG_AFE_PLL_CTRL_8812A + 3, 0x8);
	/*Enable 320M*/
	odm_write_1byte(p_dm, REG_AFE_PLL_CTRL_8812A + 3, 0xa);
	/*Enable clock*/
	odm_write_1byte(p_dm, REG_SYS_CLKR_8812A + 1, 0xfc);


	/*Release Tx pause*/
	odm_write_1byte(p_dm, REG_TXPAUSE_8812A, 0);

	/*Enable RX DMA path*/
	u1btmp = odm_read_1byte(p_dm, REG_RXDMA_CONTROL_8812A);
	odm_write_1byte(p_dm, REG_RXDMA_CONTROL_8812A, u1btmp & (~BIT(2)));
#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	/*Enable PCIe TxDMA*/
	odm_write_1byte(p_dm, REG_PCIE_CTRL_REG_8812A + 1, 0);
#endif
	/*Start Usb TxDMA*/
	RT_ENABLE_FUNC(adapter, DF_TX_BIT);
}

#endif



#endif
