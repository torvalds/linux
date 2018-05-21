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
 * This file is for 8814A TXBF mechanism
 *
 * ************************************************************ */

#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (BEAMFORMING_SUPPORT == 1)
#if (RTL8814A_SUPPORT == 1)

boolean
phydm_beamforming_set_iqgen_8814A(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8 i = 0;
	u16 counter = 0;
	u32 rf_mode[4];

	for (i = RF_PATH_A ; i < MAX_RF_PATH ; i++)
		odm_set_rf_reg(p_dm, i, RF_WE_LUT, 0x80000, 0x1);	/*RF mode table write enable*/

	while (1) {
		counter++;
		for (i = RF_PATH_A; i < MAX_RF_PATH; i++)
			odm_set_rf_reg(p_dm, i, RF_RCK_OS, 0xfffff, 0x18000);	/*Select Rx mode*/

		ODM_delay_us(2);

		for (i = RF_PATH_A; i < MAX_RF_PATH; i++)
			rf_mode[i] = odm_get_rf_reg(p_dm, i, RF_RCK_OS, 0xfffff);

		if ((rf_mode[0] == 0x18000) && (rf_mode[1] == 0x18000) && (rf_mode[2] == 0x18000) && (rf_mode[3] == 0x18000))
			break;
		else if (counter == 100) {
			PHYDM_DBG(p_dm, DBG_TXBF, ("iqgen setting fail:8814A\n"));
			return false;
		}
	}

	for (i = RF_PATH_A ; i < MAX_RF_PATH ; i++) {
		odm_set_rf_reg(p_dm, i, RF_TXPA_G1, 0xfffff, 0xBE77F); /*Set Table data*/
		odm_set_rf_reg(p_dm, i, RF_TXPA_G2, 0xfffff, 0x226BF); /*Enable TXIQGEN in Rx mode*/
	}
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G2, 0xfffff, 0xE26BF); /*Enable TXIQGEN in Rx mode*/

	for (i = RF_PATH_A; i < MAX_RF_PATH; i++)
		odm_set_rf_reg(p_dm, i, RF_WE_LUT, 0x80000, 0x0);	/*RF mode table write disable*/

	return true;

}



void
hal_txbf_8814a_set_ndpa_rate(
	void			*p_dm_void,
	u8	BW,
	u8	rate
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_write_1byte(p_dm, REG_NDPA_OPT_CTRL_8814A, BW);
	odm_write_1byte(p_dm, REG_NDPA_RATE_8814A, (u8) rate);

}
#if 0
#define PHYDM_MEMORY_MAP_BUF_READ	0x8000
#define PHYDM_CTRL_INFO_PAGE			0x660

void
phydm_data_rate_8814a(
	struct PHY_DM_STRUCT			*p_dm,
	u8				mac_id,
	u32				*data,
	u8				data_len
)
{
	u8	i = 0;
	u16	x_read_data_addr = 0;

	odm_write_2byte(p_dm, REG_PKTBUF_DBG_CTRL_8814A, PHYDM_CTRL_INFO_PAGE);
	x_read_data_addr = PHYDM_MEMORY_MAP_BUF_READ + mac_id * 32; /*Ctrl Info: 32Bytes for each macid(n)*/

	if ((x_read_data_addr < PHYDM_MEMORY_MAP_BUF_READ) || (x_read_data_addr > 0x8FFF)) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("x_read_data_addr(0x%x) is not correct!\n", x_read_data_addr));
		return;
	}

	/* Read data */
	for (i = 0; i < data_len; i++)
		*(data + i) = odm_read_2byte(p_dm, x_read_data_addr + i);

}
#endif

void
hal_txbf_8814a_get_tx_rate(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	*p_entry;
	struct _rate_adaptive_table_	*p_ra_table = &p_dm->dm_ra_table;
	struct cmn_sta_info			*p_sta = NULL;
	u8	data_rate = 0xFF;
	u8	macid = 0;

	p_entry = &(p_beam_info->beamformee_entry[p_beam_info->beamformee_cur_idx]);
	macid = (u8)p_entry->mac_id;

	p_sta = p_dm->p_phydm_sta_info[macid];
	
	if (is_sta_active(p_sta)) {
		
		data_rate = (p_sta->ra_info.curr_tx_rate) & 0x7f;	/*Bit7 indicates SGI*/
		p_beam_info->tx_bf_data_rate = data_rate;
	}

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] p_dm->tx_bf_data_rate = 0x%x\n", __func__, p_beam_info->tx_bf_data_rate));
}

void
hal_txbf_8814a_reset_tx_path(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#if DEV_BUS_TYPE == RT_USB_INTERFACE
	struct _RT_BEAMFORMING_INFO	*p_beamforming_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	beamformee_entry;
	u8	nr_index = 0, tx_ss = 0;

	if (idx < BEAMFORMEE_ENTRY_NUM)
		beamformee_entry = p_beamforming_info->beamformee_entry[idx];
	else
		return;

	if ((p_beamforming_info->last_usb_hub) != (*p_dm->hub_usb_mode)) {
		nr_index = tx_bf_nr(hal_txbf_8814a_get_ntx(p_dm), beamformee_entry.comp_steering_num_of_bfer);

		if (*p_dm->hub_usb_mode == 2) {
			if (p_dm->rf_type == RF_4T4R)
				tx_ss = 0xf;
			else if (p_dm->rf_type == RF_3T3R)
				tx_ss = 0xe;
			else
				tx_ss = 0x6;
		} else if (*p_dm->hub_usb_mode == 1)	/*USB 2.0 always 2Tx*/
			tx_ss = 0x6;
		else
			tx_ss = 0x6;

		if (tx_ss == 0xf) {
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_1_8814A, MASKBYTE3 | MASKBYTE2HIGHNIBBLE, 0x93f);
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_1_8814A, MASKDWORD, 0x93f93f0);
		} else if (tx_ss == 0xe) {
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_1_8814A, MASKBYTE3 | MASKBYTE2HIGHNIBBLE, 0x93e);
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_2_8814A, MASKDWORD, 0x93e93e0);
		} else if (tx_ss == 0x6) {
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_1_8814A, MASKBYTE3 | MASKBYTE2HIGHNIBBLE, 0x936);
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_2_8814A, MASKLWORD, 0x9360);
		}

		if (idx == 0) {
			switch (nr_index) {
			case 0:
				break;

			case 1:			/*Nsts = 2	BC*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF0_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x9366);		/*tx2path, BC*/
				break;

			case 2:			/*Nsts = 3	BCD*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF0_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x93e93ee);	/*tx3path, BCD*/
				break;

			default:			/*nr>3, same as Case 3*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF0_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x93f93ff);	/*tx4path, ABCD*/
				break;
			}
		} else	{
			switch (nr_index) {
			case 0:
				break;

			case 1:			/*Nsts = 2	BC*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF1_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x9366);		/*tx2path, BC*/
				break;

			case 2:			/*Nsts = 3	BCD*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF1_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x93e93ee);	/*tx3path, BCD*/
				break;

			default:			/*nr>3, same as Case 3*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF1_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x93f93ff);	/*tx4path, ABCD*/
				break;
			}
		}

		p_beamforming_info->last_usb_hub = *p_dm->hub_usb_mode;
	} else
		return;
#endif
}


u8
hal_txbf_8814a_get_ntx(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8		ntx = 0, tx_ss = 3;

#if DEV_BUS_TYPE == RT_USB_INTERFACE
	tx_ss = *p_dm->hub_usb_mode;
#endif
	if (tx_ss == 3 || tx_ss == 2) {
		if (p_dm->rf_type == RF_4T4R)
			ntx = 3;
		else if (p_dm->rf_type == RF_3T3R)
			ntx = 2;
		else
			ntx = 1;
	} else if (tx_ss == 1)	/*USB 2.0 always 2Tx*/
		ntx = 1;
	else
		ntx = 1;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] ntx = %d\n", __func__, ntx));
	return ntx;
}

u8
hal_txbf_8814a_get_nrx(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			nrx = 0;

	if (p_dm->rf_type == RF_4T4R)
		nrx = 3;
	else if (p_dm->rf_type == RF_3T3R)
		nrx = 2;
	else if (p_dm->rf_type == RF_2T2R)
		nrx = 1;
	else if (p_dm->rf_type == RF_2T3R)
		nrx = 2;
	else if (p_dm->rf_type == RF_2T4R)
		nrx = 3;
	else if (p_dm->rf_type == RF_1T1R)
		nrx = 0;
	else if (p_dm->rf_type == RF_1T2R)
		nrx = 1;
	else
		nrx = 0;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] nrx = %d\n", __func__, nrx));
	return nrx;
}

void
hal_txbf_8814a_rf_mode(
	void			*p_dm_void,
	struct _RT_BEAMFORMING_INFO	*p_beamforming_info,
	u8					idx
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8				nr_index = 0;
	u8				tx_ss = 3;		/*default use 3 Tx*/
	struct _RT_BEAMFORMEE_ENTRY	beamformee_entry;

	if (idx < BEAMFORMEE_ENTRY_NUM)
		beamformee_entry = p_beamforming_info->beamformee_entry[idx];
	else
		return;

	nr_index = tx_bf_nr(hal_txbf_8814a_get_ntx(p_dm), beamformee_entry.comp_steering_num_of_bfer);

	if (p_dm->rf_type == RF_1T1R)
		return;

	if (p_beamforming_info->beamformee_su_cnt > 0) {
#if DEV_BUS_TYPE == RT_USB_INTERFACE
		p_beamforming_info->last_usb_hub = *p_dm->hub_usb_mode;
		tx_ss = *p_dm->hub_usb_mode;
#endif
		if (tx_ss == 3 || tx_ss == 2) {
			if (p_dm->rf_type == RF_4T4R)
				tx_ss = 0xf;
			else if (p_dm->rf_type == RF_3T3R)
				tx_ss = 0xe;
			else
				tx_ss = 0x6;
		} else if (tx_ss == 1)	/*USB 2.0 always 2Tx*/
			tx_ss = 0x6;
		else
			tx_ss = 0x6;

		if (tx_ss == 0xf) {
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_1_8814A, MASKBYTE3 | MASKBYTE2HIGHNIBBLE, 0x93f);
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_1_8814A, MASKDWORD, 0x93f93f0);
		} else if (tx_ss == 0xe) {
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_1_8814A, MASKBYTE3 | MASKBYTE2HIGHNIBBLE, 0x93e);
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_2_8814A, MASKDWORD, 0x93e93e0);
		} else if (tx_ss == 0x6) {
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_1_8814A, MASKBYTE3 | MASKBYTE2HIGHNIBBLE, 0x936);
			odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_2_8814A, MASKLWORD, 0x9360);
		}

		/*for 8814 19ac(idx 1), 19b4(idx 0), different Tx ant setting*/
		odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF1_8814A, BIT(28) | BIT29, 0x2);			/*enable BB TxBF ant mapping register*/
		odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF1_8814A, BIT30, 0x1);			/*if Nsts > Nc don't apply V matrix*/

		if (idx == 0) {
			switch (nr_index) {
			case 0:
				break;

			case 1:			/*Nsts = 2	BC*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF0_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x9366);		/*tx2path, BC*/
				break;

			case 2:			/*Nsts = 3	BCD*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF0_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x93e93ee);	/*tx3path, BCD*/
				break;

			default:			/*nr>3, same as Case 3*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF0_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x93f93ff);	/*tx4path, ABCD*/

				break;
			}
		} else {
			switch (nr_index) {
			case 0:
				break;

			case 1:			/*Nsts = 2	BC*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF1_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x9366);		/*tx2path, BC*/
				break;

			case 2:			/*Nsts = 3	BCD*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF1_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x93e93ee);	/*tx3path, BCD*/
				break;

			default:			/*nr>3, same as Case 3*/
				odm_set_bb_reg(p_dm, REG_BB_TXBF_ANT_SET_BF1_8814A, MASKBYTE3LOWNIBBLE | MASKL3BYTES, 0x93f93ff);	/*tx4path, ABCD*/
				break;
			}
		}
	}

	if ((p_beamforming_info->beamformee_su_cnt == 0) && (p_beamforming_info->beamformer_su_cnt == 0)) {
		odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_1_8814A, MASKBYTE3 | MASKBYTE2HIGHNIBBLE, 0x932);	/*set tx_path selection for 8814a BFer bug refine*/
		odm_set_bb_reg(p_dm, REG_BB_TX_PATH_SEL_2_8814A, MASKDWORD, 0x93e9360);
	}
}
#if 0
void
hal_txbf_8814a_download_ndpa(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			u1b_tmp = 0, tmp_reg422 = 0;
	u8			bcn_valid_reg = 0, count = 0, dl_bcn_count = 0;
	u16			head_page = 0x7FE;
	boolean			is_send_beacon = false;
	u16			tx_page_bndy = LAST_ENTRY_OF_TX_PKT_BUFFER_8814A; /*default reseved 1 page for the IC type which is undefined.*/
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	*p_beam_entry = p_beam_info->beamformee_entry + idx;
	struct _ADAPTER		*adapter = p_dm->adapter;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	*p_dm->p_is_fw_dw_rsvd_page_in_progress = true;
#endif
	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	phydm_get_hal_def_var_handler_interface(p_dm, HAL_DEF_TX_PAGE_BOUNDARY, (u16 *)&tx_page_bndy);

	/*Set REG_CR bit 8. DMA beacon by SW.*/
	u1b_tmp = odm_read_1byte(p_dm, REG_CR_8814A + 1);
	odm_write_1byte(p_dm,  REG_CR_8814A + 1, (u1b_tmp | BIT(0)));


	/*Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.*/
	tmp_reg422 = odm_read_1byte(p_dm, REG_FWHW_TXQ_CTRL_8814A + 2);
	odm_write_1byte(p_dm, REG_FWHW_TXQ_CTRL_8814A + 2,  tmp_reg422 & (~BIT(6)));

	if (tmp_reg422 & BIT(6)) {
		PHYDM_DBG(p_dm, DBG_TXBF, ("%s: There is an adapter is sending beacon.\n", __func__));
		is_send_beacon = true;
	}

	/*0x204[11:0]	Beacon Head for TXDMA*/
	odm_write_2byte(p_dm, REG_FIFOPAGE_CTRL_2_8814A, head_page);

	do {
		/*Clear beacon valid check bit.*/
		bcn_valid_reg = odm_read_1byte(p_dm, REG_FIFOPAGE_CTRL_2_8814A + 1);
		odm_write_1byte(p_dm, REG_FIFOPAGE_CTRL_2_8814A + 1, (bcn_valid_reg | BIT(7)));

		/*download NDPA rsvd page.*/
		if (p_beam_entry->beamform_entry_cap & BEAMFORMER_CAP_VHT_SU)
			beamforming_send_vht_ndpa_packet(p_dm, p_beam_entry->mac_addr, p_beam_entry->AID, p_beam_entry->sound_bw, BEACON_QUEUE);
		else
			beamforming_send_ht_ndpa_packet(p_dm, p_beam_entry->mac_addr, p_beam_entry->sound_bw, BEACON_QUEUE);

		/*check rsvd page download OK.*/
		bcn_valid_reg = odm_read_1byte(p_dm, REG_FIFOPAGE_CTRL_2_8814A + 1);
		count = 0;
		while (!(bcn_valid_reg & BIT(7)) && count < 20) {
			count++;
			ODM_delay_ms(10);
			bcn_valid_reg = odm_read_1byte(p_dm, REG_FIFOPAGE_CTRL_2_8814A + 2);
		}
		dl_bcn_count++;
	} while (!(bcn_valid_reg & BIT(7)) && dl_bcn_count < 5);

	if (!(bcn_valid_reg & BIT(7)))
		PHYDM_DBG(p_dm, DBG_TXBF, ("%s Download RSVD page failed!\n", __func__));

	/*0x204[11:0]	Beacon Head for TXDMA*/
	odm_write_2byte(p_dm, REG_FIFOPAGE_CTRL_2_8814A, tx_page_bndy);

	/*To make sure that if there exists an adapter which would like to send beacon.*/
	/*If exists, the origianl value of 0x422[6] will be 1, we should check this to*/
	/*prevent from setting 0x422[6] to 0 after download reserved page, or it will cause */
	/*the beacon cannot be sent by HW.*/
	/*2010.06.23. Added by tynli.*/
	if (is_send_beacon)
		odm_write_1byte(p_dm, REG_FWHW_TXQ_CTRL_8814A + 2, tmp_reg422);

	/*Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli.*/
	/*Clear CR[8] or beacon packet will not be send to TxBuf anymore.*/
	u1b_tmp = odm_read_1byte(p_dm, REG_CR_8814A + 1);
	odm_write_1byte(p_dm, REG_CR_8814A + 1, (u1b_tmp & (~BIT(0))));

	p_beam_entry->beamform_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSED;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	*p_dm->p_is_fw_dw_rsvd_page_in_progress = false;
#endif
}

void
hal_txbf_8814a_fw_txbf_cmd(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	idx, period = 0;
	u8	PageNum0 = 0xFF, PageNum1 = 0xFF;
	u8	u1_tx_bf_parm[3] = {0};
	struct _RT_BEAMFORMING_INFO *p_beam_info = &p_dm->beamforming_info;

	for (idx = 0; idx < BEAMFORMEE_ENTRY_NUM; idx++) {
		if (p_beam_info->beamformee_entry[idx].is_used && p_beam_info->beamformee_entry[idx].beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			if (p_beam_info->beamformee_entry[idx].is_sound) {
				PageNum0 = 0xFE;
				PageNum1 = 0x07;
				period = (u8)(p_beam_info->beamformee_entry[idx].sound_period);
			} else if (PageNum0 == 0xFF) {
				PageNum0 = 0xFF; /*stop sounding*/
				PageNum1 = 0x0F;
			}
		}
	}

	u1_tx_bf_parm[0] = PageNum0;
	u1_tx_bf_parm[1] = PageNum1;
	u1_tx_bf_parm[2] = period;
	odm_fill_h2c_cmd(p_dm, PHYDM_H2C_TXBF, 3, u1_tx_bf_parm);

	PHYDM_DBG(p_dm, DBG_TXBF,
		("[%s] PageNum0 = %d, PageNum1 = %d period = %d\n", __func__, PageNum0, PageNum1, period));
}
#endif
void
hal_txbf_8814a_enter(
	void			*p_dm_void,
	u8				bfer_bfee_idx
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8					i = 0;
	u8					bfer_idx = (bfer_bfee_idx & 0xF0) >> 4;
	u8					bfee_idx = (bfer_bfee_idx & 0xF);
	struct _RT_BEAMFORMING_INFO	*p_beamforming_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	beamformee_entry;
	struct _RT_BEAMFORMER_ENTRY	beamformer_entry;
	u16					sta_id = 0, csi_param = 0;
	u8					nc_index = 0, nr_index = 0, grouping = 0, codebookinfo = 0, coefficientsize = 0;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] bfer_idx=%d, bfee_idx=%d\n", __func__, bfer_idx, bfee_idx));
	odm_set_mac_reg(p_dm, REG_SND_PTCL_CTRL_8814A, MASKBYTE1 | MASKBYTE2, 0x0202);

	if ((p_beamforming_info->beamformer_su_cnt > 0) && (bfer_idx < BEAMFORMER_ENTRY_NUM)) {
		beamformer_entry = p_beamforming_info->beamformer_entry[bfer_idx];
		/*Sounding protocol control*/
		odm_write_1byte(p_dm, REG_SND_PTCL_CTRL_8814A, 0xDB);

		/*MAC address/Partial AID of Beamformer*/
		if (bfer_idx == 0) {
			for (i = 0; i < 6 ; i++)
				odm_write_1byte(p_dm, (REG_ASSOCIATED_BFMER0_INFO_8814A + i), beamformer_entry.mac_addr[i]);
		} else {
			for (i = 0; i < 6 ; i++)
				odm_write_1byte(p_dm, (REG_ASSOCIATED_BFMER1_INFO_8814A + i), beamformer_entry.mac_addr[i]);
		}

		/*CSI report parameters of Beamformer*/
		nc_index = hal_txbf_8814a_get_nrx(p_dm);	/*for 8814A nrx = 3(4 ant), min=0(1 ant)*/
		nr_index = beamformer_entry.num_of_sounding_dim;	/*0x718[7] = 1 use Nsts, 0x718[7] = 0 use reg setting. as Bfee, we use Nsts, so nr_index don't care*/

		grouping = 0;

		/*for ac = 1, for n = 3*/
		if (beamformer_entry.beamform_entry_cap & BEAMFORMEE_CAP_VHT_SU)
			codebookinfo = 1;
		else if (beamformer_entry.beamform_entry_cap & BEAMFORMEE_CAP_HT_EXPLICIT)
			codebookinfo = 3;

		coefficientsize = 3;

		csi_param = (u16)((coefficientsize << 10) | (codebookinfo << 8) | (grouping << 6) | (nr_index << 3) | (nc_index));

		if (bfer_idx == 0)
			odm_write_2byte(p_dm, REG_CSI_RPT_PARAM_BW20_8814A, csi_param);
		else
			odm_write_2byte(p_dm, REG_CSI_RPT_PARAM_BW20_8814A + 2, csi_param);
		/*ndp_rx_standby_timer, 8814 need > 0x56, suggest from Dvaid*/
		odm_write_1byte(p_dm, REG_SND_PTCL_CTRL_8814A + 3, 0x40);

	}

	if ((p_beamforming_info->beamformee_su_cnt > 0) && (bfee_idx < BEAMFORMEE_ENTRY_NUM)) {
		beamformee_entry = p_beamforming_info->beamformee_entry[bfee_idx];

		hal_txbf_8814a_rf_mode(p_dm, p_beamforming_info, bfee_idx);

		if (phydm_acting_determine(p_dm, phydm_acting_as_ibss))
			sta_id = beamformee_entry.mac_id;
		else
			sta_id = beamformee_entry.p_aid;

		/*P_AID of Beamformee & enable NDPA transmission & enable NDPA interrupt*/
		if (bfee_idx == 0) {
			odm_write_2byte(p_dm, REG_TXBF_CTRL_8814A, sta_id);
			odm_write_1byte(p_dm, REG_TXBF_CTRL_8814A + 3, odm_read_1byte(p_dm, REG_TXBF_CTRL_8814A + 3) | BIT(4) | BIT(6) | BIT(7));
		} else
			odm_write_2byte(p_dm, REG_TXBF_CTRL_8814A + 2, sta_id | BIT(14) | BIT(15) | BIT(12));

		/*CSI report parameters of Beamformee*/
		if (bfee_idx == 0) {
			/*Get BIT24 & BIT25*/
			u8	tmp = odm_read_1byte(p_dm, REG_ASSOCIATED_BFMEE_SEL_8814A + 3) & 0x3;

			odm_write_1byte(p_dm, REG_ASSOCIATED_BFMEE_SEL_8814A + 3, tmp | 0x60);
			odm_write_2byte(p_dm, REG_ASSOCIATED_BFMEE_SEL_8814A, sta_id | BIT(9));
		} else
			odm_write_2byte(p_dm, REG_ASSOCIATED_BFMEE_SEL_8814A + 2, sta_id | 0xE200);	/*Set BIT25*/

		phydm_beamforming_notify(p_dm);
	}

}


void
hal_txbf_8814a_leave(
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

	/*Clear P_AID of Beamformee*/
	/*Clear MAC address of Beamformer*/
	/*Clear Associated Bfmee Sel*/

	if (beamformer_entry.beamform_entry_cap == BEAMFORMING_CAP_NONE) {
		odm_write_1byte(p_dm, REG_SND_PTCL_CTRL_8814A, 0xD8);
		if (idx == 0) {
			odm_write_4byte(p_dm, REG_ASSOCIATED_BFMER0_INFO_8814A, 0);
			odm_write_2byte(p_dm, REG_ASSOCIATED_BFMER0_INFO_8814A + 4, 0);
			odm_write_2byte(p_dm, REG_CSI_RPT_PARAM_BW20_8814A, 0);
		} else {
			odm_write_4byte(p_dm, REG_ASSOCIATED_BFMER1_INFO_8814A, 0);
			odm_write_2byte(p_dm, REG_ASSOCIATED_BFMER1_INFO_8814A + 4, 0);
			odm_write_2byte(p_dm, REG_CSI_RPT_PARAM_BW20_8814A + 2, 0);
		}
	}

	if (beamformee_entry.beamform_entry_cap == BEAMFORMING_CAP_NONE) {
		hal_txbf_8814a_rf_mode(p_dm, p_beamforming_info, idx);
		if (idx == 0) {
			odm_write_2byte(p_dm, REG_TXBF_CTRL_8814A, 0x0);
			odm_write_1byte(p_dm, REG_TXBF_CTRL_8814A + 3, odm_read_1byte(p_dm, REG_TXBF_CTRL_8814A + 3) | BIT(4) | BIT(6) | BIT(7));
			odm_write_2byte(p_dm, REG_ASSOCIATED_BFMEE_SEL_8814A, 0);
		} else {
			odm_write_2byte(p_dm, REG_TXBF_CTRL_8814A + 2, 0x0 | BIT(14) | BIT(15) | BIT(12));

			odm_write_2byte(p_dm, REG_ASSOCIATED_BFMEE_SEL_8814A + 2, odm_read_2byte(p_dm, REG_ASSOCIATED_BFMEE_SEL_8814A + 2) & 0x60);
		}
	}
}

void
hal_txbf_8814a_status(
	void			*p_dm_void,
	u8				idx
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u16					beam_ctrl_val, tmp_val;
	u32					beam_ctrl_reg;
	struct _RT_BEAMFORMING_INFO	*p_beamforming_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	beamform_entry;

	if (idx < BEAMFORMEE_ENTRY_NUM)
		beamform_entry = p_beamforming_info->beamformee_entry[idx];
	else
		return;

	if (phydm_acting_determine(p_dm, phydm_acting_as_ibss))
		beam_ctrl_val = beamform_entry.mac_id;
	else
		beam_ctrl_val = beamform_entry.p_aid;

	PHYDM_DBG(p_dm, DBG_TXBF, ("@%s, beamform_entry.beamform_entry_state = %d", __func__, beamform_entry.beamform_entry_state));

	if (idx == 0)
		beam_ctrl_reg = REG_TXBF_CTRL_8814A;
	else {
		beam_ctrl_reg = REG_TXBF_CTRL_8814A + 2;
		beam_ctrl_val |= BIT(12) | BIT(14) | BIT(15);
	}

	if ((beamform_entry.beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED) && (p_beamforming_info->apply_v_matrix == true)) {
		if (beamform_entry.sound_bw == CHANNEL_WIDTH_20)
			beam_ctrl_val |= BIT(9);
		else if (beamform_entry.sound_bw == CHANNEL_WIDTH_40)
			beam_ctrl_val |= (BIT(9) | BIT(10));
		else if (beamform_entry.sound_bw == CHANNEL_WIDTH_80)
			beam_ctrl_val |= (BIT(9) | BIT(10) | BIT(11));
	} else {
		PHYDM_DBG(p_dm, DBG_TXBF, ("@%s, Don't apply Vmatrix",  __func__));
		beam_ctrl_val &= ~(BIT(9) | BIT(10) | BIT(11));
	}

	odm_write_2byte(p_dm, beam_ctrl_reg, beam_ctrl_val);
	/*disable NDP packet use beamforming */
	tmp_val = odm_read_2byte(p_dm, REG_TXBF_CTRL_8814A);
	odm_write_2byte(p_dm, REG_TXBF_CTRL_8814A, tmp_val | BIT(15));

}





void
hal_txbf_8814a_fw_txbf(
	void			*p_dm_void,
	u8				idx
)
{
#if 0
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _RT_BEAMFORMING_INFO	*p_beam_info = &p_dm->beamforming_info;
	struct _RT_BEAMFORMEE_ENTRY	*p_beam_entry = p_beam_info->beamformee_entry + idx;

	PHYDM_DBG(p_dm, DBG_TXBF, ("[%s] Start!\n", __func__));

	if (p_beam_entry->beamform_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSING)
		hal_txbf_8814a_download_ndpa(p_dm, idx);

	hal_txbf_8814a_fw_txbf_cmd(p_dm);
#endif
}

#endif	/* (RTL8814A_SUPPORT == 1)*/

#endif
