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

/*************************************************************
 * include files
 ************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef CONFIG_PATH_DIVERSITY
#if RTL8814A_SUPPORT
void phydm_dtp_fix_tx_path(
	void *dm_void,
	u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	u8 i, num_enable_path = 0;

	if (path == p_div->pre_tx_path)
		return;
	else
		p_div->pre_tx_path = path;

	odm_set_bb_reg(dm, R_0x93c, BIT(18) | BIT(19), 3);

	for (i = 0; i < 4; i++) {
		if (path & BIT(i))
			num_enable_path++;
	}
	PHYDM_DBG(dm, DBG_PATH_DIV, " number of turn-on path : (( %d ))\n",
		  num_enable_path);

	if (num_enable_path == 1) {
		odm_set_bb_reg(dm, R_0x93c, 0xf00000, path);

		if (path == BB_PATH_A) { /* @1-1 */
			PHYDM_DBG(dm, DBG_PATH_DIV, " Turn on path (( A ))\n");
			odm_set_bb_reg(dm, R_0x93c, BIT(25) | BIT(24), 0);
		} else if (path == BB_PATH_B) { /* @1-2 */
			PHYDM_DBG(dm, DBG_PATH_DIV, " Turn on path (( B ))\n");
			odm_set_bb_reg(dm, R_0x93c, BIT(27) | BIT(26), 0);
		} else if (path == BB_PATH_C) { /* @1-3 */
			PHYDM_DBG(dm, DBG_PATH_DIV, " Turn on path (( C ))\n");
			odm_set_bb_reg(dm, R_0x93c, BIT(29) | BIT(28), 0);

		} else if (path == BB_PATH_D) { /* @1-4 */
			PHYDM_DBG(dm, DBG_PATH_DIV, " Turn on path (( D ))\n");
			odm_set_bb_reg(dm, R_0x93c, BIT(31) | BIT(30), 0);
		}

	} else if (num_enable_path == 2) {
		odm_set_bb_reg(dm, R_0x93c, 0xf00000, path);
		odm_set_bb_reg(dm, R_0x940, 0xf0, path);

		if (path == (BB_PATH_AB)) { /* @2-1 */
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  " Turn on path (( A B ))\n");
			/* set for 1ss */
			odm_set_bb_reg(dm, R_0x93c, BIT(25) | BIT(24), 0);
			odm_set_bb_reg(dm, R_0x93c, BIT(27) | BIT(26), 1);
			/* set for 2ss */
			odm_set_bb_reg(dm, R_0x940, BIT(9) | BIT(8), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(11) | BIT(10), 1);
		} else if (path == BB_PATH_AC) { /* @2-2 */
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  " Turn on path (( A C ))\n");
			/* set for 1ss */
			odm_set_bb_reg(dm, R_0x93c, BIT(25) | BIT(24), 0);
			odm_set_bb_reg(dm, R_0x93c, BIT(29) | BIT(28), 1);
			/* set for 2ss */
			odm_set_bb_reg(dm, R_0x940, BIT(9) | BIT(8), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(13) | BIT(12), 1);
		} else if (path == BB_PATH_AD) { /* @2-3 */
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  " Turn on path (( A D ))\n");
			/* set for 1ss */
			odm_set_bb_reg(dm, R_0x93c, BIT(25) | BIT(24), 0);
			odm_set_bb_reg(dm, R_0x93c, BIT(31) | BIT(30), 1);
			/* set for 2ss */
			odm_set_bb_reg(dm, R_0x940, BIT(9) | BIT(8), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(15) | BIT(14), 1);
		} else if (path == BB_PATH_BC) { /* @2-4 */
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  " Turn on path (( B C ))\n");
			/* set for 1ss */
			odm_set_bb_reg(dm, R_0x93c, BIT(27) | BIT(26), 0);
			odm_set_bb_reg(dm, R_0x93c, BIT(29) | BIT(28), 1);
			/* set for 2ss */
			odm_set_bb_reg(dm, R_0x940, BIT(11) | BIT(10), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(13) | BIT(12), 1);
		} else if (path == BB_PATH_BD) { /* @2-5 */
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  " Turn on path (( B D ))\n");
			/* set for 1ss */
			odm_set_bb_reg(dm, R_0x93c, BIT(27) | BIT(26), 0);
			odm_set_bb_reg(dm, R_0x93c, BIT(31) | BIT(30), 1);
			/* set for 2ss */
			odm_set_bb_reg(dm, R_0x940, BIT(11) | BIT(10), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(15) | BIT(14), 1);
		} else if (path == BB_PATH_CD) { /* @2-6 */
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  " Turn on path (( C D ))\n");
			/* set for 1ss */
			odm_set_bb_reg(dm, R_0x93c, BIT(29) | BIT(28), 0);
			odm_set_bb_reg(dm, R_0x93c, BIT(31) | BIT(30), 1);
			/* set for 2ss */
			odm_set_bb_reg(dm, R_0x940, BIT(13) | BIT(12), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(15) | BIT(14), 1);
		}

	} else if (num_enable_path == 3) {
		odm_set_bb_reg(dm, R_0x93c, 0xf00000, path);
		odm_set_bb_reg(dm, R_0x940, 0xf0, path);
		odm_set_bb_reg(dm, R_0x940, 0xf0000, path);

		if (path == BB_PATH_ABC) { /* @3-1 */
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  " Turn on path (( A B C))\n");
			/* set for 1ss */
			odm_set_bb_reg(dm, R_0x93c, BIT(25) | BIT(24), 0);
			odm_set_bb_reg(dm, R_0x93c, BIT(27) | BIT(26), 1);
			odm_set_bb_reg(dm, R_0x93c, BIT(29) | BIT(28), 2);
			/* set for 2ss */
			odm_set_bb_reg(dm, R_0x940, BIT(9) | BIT(8), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(11) | BIT(10), 1);
			odm_set_bb_reg(dm, R_0x940, BIT(13) | BIT(12), 2);
			/* set for 3ss */
			odm_set_bb_reg(dm, R_0x940, BIT(21) | BIT(20), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(23) | BIT(22), 1);
			odm_set_bb_reg(dm, R_0x940, BIT(25) | BIT(24), 2);
		} else if (path == BB_PATH_ABD) { /* @3-2 */
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  " Turn on path (( A B D ))\n");
			/* set for 1ss */
			odm_set_bb_reg(dm, R_0x93c, BIT(25) | BIT(24), 0);
			odm_set_bb_reg(dm, R_0x93c, BIT(27) | BIT(26), 1);
			odm_set_bb_reg(dm, R_0x93c, BIT(31) | BIT(30), 2);
			/* set for 2ss */
			odm_set_bb_reg(dm, R_0x940, BIT(9) | BIT(8), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(11) | BIT(10), 1);
			odm_set_bb_reg(dm, R_0x940, BIT(15) | BIT(14), 2);
			/* set for 3ss */
			odm_set_bb_reg(dm, R_0x940, BIT(21) | BIT(20), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(23) | BIT(22), 1);
			odm_set_bb_reg(dm, R_0x940, BIT(27) | BIT(26), 2);

		} else if (path == BB_PATH_ACD) { /* @3-3 */
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  " Turn on path (( A C D ))\n");
			/* set for 1ss */
			odm_set_bb_reg(dm, R_0x93c, BIT(25) | BIT(24), 0);
			odm_set_bb_reg(dm, R_0x93c, BIT(29) | BIT(28), 1);
			odm_set_bb_reg(dm, R_0x93c, BIT(31) | BIT(30), 2);
			/* set for 2ss */
			odm_set_bb_reg(dm, R_0x940, BIT(9) | BIT(8), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(13) | BIT(12), 1);
			odm_set_bb_reg(dm, R_0x940, BIT(15) | BIT(14), 2);
			/* set for 3ss */
			odm_set_bb_reg(dm, R_0x940, BIT(21) | BIT(20), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(25) | BIT(24), 1);
			odm_set_bb_reg(dm, R_0x940, BIT(27) | BIT(26), 2);
		} else if (path == BB_PATH_BCD) { /* @3-4 */
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  " Turn on path (( B C D))\n");
			/* set for 1ss */
			odm_set_bb_reg(dm, R_0x93c, BIT(27) | BIT(26), 0);
			odm_set_bb_reg(dm, R_0x93c, BIT(29) | BIT(28), 1);
			odm_set_bb_reg(dm, R_0x93c, BIT(31) | BIT(30), 2);
			/* set for 2ss */
			odm_set_bb_reg(dm, R_0x940, BIT(11) | BIT(10), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(13) | BIT(12), 1);
			odm_set_bb_reg(dm, R_0x940, BIT(15) | BIT(14), 2);
			/* set for 3ss */
			odm_set_bb_reg(dm, R_0x940, BIT(23) | BIT(22), 0);
			odm_set_bb_reg(dm, R_0x940, BIT(25) | BIT(24), 1);
			odm_set_bb_reg(dm, R_0x940, BIT(27) | BIT(26), 2);
		}
	} else if (num_enable_path == 4)
		PHYDM_DBG(dm, DBG_PATH_DIV, " Turn on path ((A  B C D))\n");
}

void phydm_find_default_path(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	u32 rssi_a = 0, rssi_b = 0, rssi_c = 0, rssi_d = 0, rssi_bcd = 0;
	u32 rssi_total_a = 0, rssi_total_b = 0;
	u32 rssi_total_c = 0, rssi_total_d = 0;

	/* @2 Default path Selection By RSSI */

	rssi_a = (p_div->path_a_cnt_all > 0) ?
		 (p_div->path_a_sum_all / p_div->path_a_cnt_all) : 0;
	rssi_b = (p_div->path_b_cnt_all > 0) ?
		 (p_div->path_b_sum_all / p_div->path_b_cnt_all) : 0;
	rssi_c = (p_div->path_c_cnt_all > 0) ?
		 (p_div->path_c_sum_all / p_div->path_c_cnt_all) : 0;
	rssi_d = (p_div->path_d_cnt_all > 0) ?
		 (p_div->path_d_sum_all / p_div->path_d_cnt_all) : 0;

	p_div->path_a_sum_all = 0;
	p_div->path_a_cnt_all = 0;
	p_div->path_b_sum_all = 0;
	p_div->path_b_cnt_all = 0;
	p_div->path_c_sum_all = 0;
	p_div->path_c_cnt_all = 0;
	p_div->path_d_sum_all = 0;
	p_div->path_d_cnt_all = 0;

	if (p_div->use_path_a_as_default_ant == 1) {
		rssi_bcd = (rssi_b + rssi_c + rssi_d) / 3;

		if ((rssi_a + ANT_DECT_RSSI_TH) > rssi_bcd) {
			p_div->is_path_a_exist = true;
			p_div->default_path = PATH_A;
		} else {
			p_div->is_path_a_exist = false;
		}
	} else {
		if (rssi_a >= rssi_b &&
		    rssi_a >= rssi_c &&
		    rssi_a >= rssi_d)
			p_div->default_path = PATH_A;
		else if ((rssi_b >= rssi_c) && (rssi_b >= rssi_d))
			p_div->default_path = PATH_B;
		else if (rssi_c >= rssi_d)
			p_div->default_path = PATH_C;
		else
			p_div->default_path = PATH_D;
	}
}

void phydm_candidate_dtp_update(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;

	p_div->num_candidate = 3;

	if (p_div->use_path_a_as_default_ant == 1) {
		if (p_div->num_tx_path == 3) {
			if (p_div->is_path_a_exist) {
				p_div->ant_candidate_1 = BB_PATH_ABC;
				p_div->ant_candidate_2 = BB_PATH_ABD;
				p_div->ant_candidate_3 = BB_PATH_ACD;
			} else { /* use path BCD */
				p_div->num_candidate = 1;
				phydm_dtp_fix_tx_path(dm, BB_PATH_BCD);
				return;
			}
		} else if (p_div->num_tx_path == 2) {
			if (p_div->is_path_a_exist) {
				p_div->ant_candidate_1 = BB_PATH_AB;
				p_div->ant_candidate_2 = BB_PATH_AC;
				p_div->ant_candidate_3 = BB_PATH_AD;
			} else {
				p_div->ant_candidate_1 = BB_PATH_BC;
				p_div->ant_candidate_2 = BB_PATH_BD;
				p_div->ant_candidate_3 = BB_PATH_CD;
			}
		}
	} else {
		/* @2 3 TX mode */
		if (p_div->num_tx_path == 3) { /* @choose 3 ant form 4 */
			if (p_div->default_path == PATH_A) {
			/* @choose 2 ant form 3 */
				p_div->ant_candidate_1 = BB_PATH_ABC;
				p_div->ant_candidate_2 = BB_PATH_ABD;
				p_div->ant_candidate_3 = BB_PATH_ACD;
			} else if (p_div->default_path == PATH_B) {
				p_div->ant_candidate_1 = BB_PATH_ABC;
				p_div->ant_candidate_2 = BB_PATH_ABD;
				p_div->ant_candidate_3 = BB_PATH_BCD;
			} else if (p_div->default_path == PATH_C) {
				p_div->ant_candidate_1 = BB_PATH_ABC;
				p_div->ant_candidate_2 = BB_PATH_ACD;
				p_div->ant_candidate_3 = BB_PATH_BCD;
			} else if (p_div->default_path == PATH_D) {
				p_div->ant_candidate_1 = BB_PATH_ABD;
				p_div->ant_candidate_2 = BB_PATH_ACD;
				p_div->ant_candidate_3 = BB_PATH_BCD;
			}
		}

		/* @2 2 TX mode */
		else if (p_div->num_tx_path == 2) { /* @choose 2 ant form 4 */
			if (p_div->default_path == PATH_A) {
			/* @choose 2 ant form 3 */
				p_div->ant_candidate_1 = BB_PATH_AB;
				p_div->ant_candidate_2 = BB_PATH_AC;
				p_div->ant_candidate_3 = BB_PATH_AD;
			} else if (p_div->default_path == PATH_B) {
				p_div->ant_candidate_1 = BB_PATH_AB;
				p_div->ant_candidate_2 = BB_PATH_BC;
				p_div->ant_candidate_3 = BB_PATH_BD;
			} else if (p_div->default_path == PATH_C) {
				p_div->ant_candidate_1 = BB_PATH_AC;
				p_div->ant_candidate_2 = BB_PATH_BC;
				p_div->ant_candidate_3 = BB_PATH_CD;
			} else if (p_div->default_path == PATH_D) {
				p_div->ant_candidate_1 = BB_PATH_AD;
				p_div->ant_candidate_2 = BB_PATH_BD;
				p_div->ant_candidate_3 = BB_PATH_CD;
			}
		}
	}
}

void phydm_dynamic_tx_path(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;

	struct sta_info *entry;
	u32 i;
	u8 num_client = 0;
	u8 h2c_parameter[6] = {0};

	if (!dm->is_linked) { /* @is_linked==False */
		PHYDM_DBG(dm, DBG_PATH_DIV, "DTP_8814 [No Link!!!]\n");

		if (p_div->is_become_linked) {
			PHYDM_DBG(dm, DBG_PATH_DIV, "[Be disconnected]---->\n");
			p_div->is_become_linked = dm->is_linked;
		}
		return;
	} else {
		if (!p_div->is_become_linked) {
			PHYDM_DBG(dm, DBG_PATH_DIV, " [Be Linked !!!]----->\n");
			p_div->is_become_linked = dm->is_linked;
		}
	}

	/* @2 [period CTRL] */
	if (p_div->dtp_period >= 2) {
		p_div->dtp_period = 0;
	} else {
		p_div->dtp_period++;
		return;
	}

	/* @2 [Fix path] */
	if (dm->path_select != PHYDM_AUTO_PATH)
		return;

/* @2 [Check Bfer] */
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#ifdef PHYDM_BEAMFORMING_SUPPORT
	{
		enum beamforming_cap beamform_cap = (dm->beamforming_info.beamform_cap);

		if (beamform_cap & BEAMFORMER_CAP) { /* @BFmer On  &&   Div On->Div Off */
			if (p_div->fix_path_bfer == 0) {
				PHYDM_DBG(dm, DBG_PATH_DIV,
					  "[ PathDiv : OFF ]   BFmer ==1\n");
				p_div->fix_path_bfer = 1;
			}
			return;
		} else { /* @BFmer Off   &&   Div Off->Div On */
			if (p_div->fix_path_bfer == 1) {
				PHYDM_DBG(dm, DBG_PATH_DIV,
					  "[ PathDiv : ON ]   BFmer ==0\n");
				p_div->fix_path_bfer = 0;
			}
		}
	}
#endif
#endif

	if (p_div->use_path_a_as_default_ant == 1) {
		phydm_find_default_path(dm);
		phydm_candidate_dtp_update(dm);
	} else {
		if (p_div->phydm_dtp_state == PHYDM_DTP_INIT) {
			phydm_find_default_path(dm);
			phydm_candidate_dtp_update(dm);
			p_div->phydm_dtp_state = PHYDM_DTP_RUNNING_1;
		}

		else if (p_div->phydm_dtp_state == PHYDM_DTP_RUNNING_1) {
			p_div->dtp_check_patha_counter++;

			if (p_div->dtp_check_patha_counter >=
			    NUM_RESET_DTP_PERIOD) {
				p_div->dtp_check_patha_counter = 0;
				p_div->phydm_dtp_state = PHYDM_DTP_INIT;
			}
#if 0
			/* @2 Search space update */
			else {
				/* @1.  find the worst candidate */


				/* @2. repalce the worst candidate */
			}
#endif
		}
	}

	/* @2 Dynamic path Selection H2C */

	if (p_div->num_candidate == 1) {
		return;
	} else {
		h2c_parameter[0] = p_div->num_candidate;
		h2c_parameter[1] = p_div->num_tx_path;
		h2c_parameter[2] = p_div->ant_candidate_1;
		h2c_parameter[3] = p_div->ant_candidate_2;
		h2c_parameter[4] = p_div->ant_candidate_3;

		odm_fill_h2c_cmd(dm, PHYDM_H2C_DYNAMIC_TX_PATH, 6, h2c_parameter);
	}
}

void phydm_dynamic_tx_path_init(
	void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	void *adapter = dm->adapter;
	u8 search_space_2[NUM_CHOOSE2_FROM4] = {BB_PATH_AB, BB_PATH_AC, BB_PATH_AD, BB_PATH_BC, BB_PATH_BD, BB_PATH_CD};
	u8 search_space_3[NUM_CHOOSE3_FROM4] = {BB_PATH_BCD, BB_PATH_ACD, BB_PATH_ABD, BB_PATH_ABC};

#if ((DM_ODM_SUPPORT_TYPE == ODM_WIN) && USB_SWITCH_SUPPORT)
	p_div->is_u3_mode = (*dm->hub_usb_mode == 2) ? 1 : 0;
	PHYDM_DBG(dm, DBG_PATH_DIV, "[WIN USB] is_u3_mode = (( %d ))\n",
		  p_div->is_u3_mode);
#else
	p_div->is_u3_mode = 1;
#endif
	PHYDM_DBG(dm, DBG_PATH_DIV, "Dynamic TX path Init 8814\n");

	memcpy(&p_div->search_space_2[0], &search_space_2[0],
	       NUM_CHOOSE2_FROM4);
	memcpy(&p_div->search_space_3[0], &search_space_3[0],
	       NUM_CHOOSE3_FROM4);

	p_div->use_path_a_as_default_ant = 1;
	p_div->phydm_dtp_state = PHYDM_DTP_INIT;
	dm->path_select = PHYDM_AUTO_PATH;
	p_div->phydm_path_div_type = PHYDM_4R_PATH_DIV;

	if (p_div->is_u3_mode) {
		p_div->num_tx_path = 3;
		phydm_dtp_fix_tx_path(dm, BB_PATH_BCD); /* @3TX  Set Init TX path*/

	} else {
		p_div->num_tx_path = 2;
		phydm_dtp_fix_tx_path(dm, BB_PATH_BC); /* @2TX // Set Init TX path*/
	}
}

void phydm_process_rssi_for_path_div_8814a(void *dm_void, void *phy_info_void,
					   void *pkt_info_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_phyinfo_struct *phy_info = NULL;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;

	phy_info = (struct phydm_phyinfo_struct *)phy_info_void;
	pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;

	if (!(pktinfo->is_packet_to_self || pktinfo->is_packet_match_bssid))
		return;

	if (pktinfo->data_rate <= ODM_RATE11M)
		return;

	if (p_div->phydm_path_div_type == PHYDM_4R_PATH_DIV) {
		p_div->path_a_sum_all += phy_info->rx_mimo_signal_strength[0];
		p_div->path_a_cnt_all++;

		p_div->path_b_sum_all += phy_info->rx_mimo_signal_strength[1];
		p_div->path_b_cnt_all++;

		p_div->path_c_sum_all += phy_info->rx_mimo_signal_strength[2];
		p_div->path_c_cnt_all++;

		p_div->path_d_sum_all += phy_info->rx_mimo_signal_strength[3];
		p_div->path_d_cnt_all++;
	}
}

void phydm_pathdiv_debug_8814a(void *dm_void, char input[][16], u32 *_used,
			       char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 dm_value[10] = {0};
	u8 i, input_idx = 0;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_HEX, &dm_value[i]);
			input_idx++;
		}
	}

	if (input_idx == 0)
		return;

	dm->path_select = (u8)(dm_value[0] & 0xf);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "Path_select = (( 0x%x ))\n", dm->path_select);

	/* @2 [Fix path] */
	if (dm->path_select != PHYDM_AUTO_PATH) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Turn on path  [%s%s%s%s]\n",
			 ((dm->path_select) & 0x1) ? "A" : "",
			 ((dm->path_select) & 0x2) ? "B" : "",
			 ((dm->path_select) & 0x4) ? "C" : "",
			 ((dm->path_select) & 0x8) ? "D" : "");

		phydm_dtp_fix_tx_path(dm, dm->path_select);
	} else {
		PDM_SNPF(out_len, used, output + used, out_len - used, "%s\n",
			 "Auto path");
	}

	*_used = used;
	*_out_len = out_len;
}

#endif /* @#if RTL8814A_SUPPORT */

#if RTL8812A_SUPPORT
void phydm_update_tx_path_8812a(void *dm_void, enum bb_path path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;

	if (p_div->default_tx_path != path) {
		PHYDM_DBG(dm, DBG_PATH_DIV, "Need to Update Tx path\n");

		if (path == BB_PATH_A) {
			/*Tx by Reg*/
			odm_set_bb_reg(dm, R_0x80c, 0xFFF0, 0x111);
			/*Resp Tx by Txinfo*/
			odm_set_bb_reg(dm, R_0x6d8, 0xc0, 1);
		} else {
			/*Tx by Reg*/
			odm_set_bb_reg(dm, R_0x80c, 0xFFF0, 0x222);
			 /*Resp Tx by Txinfo*/
			odm_set_bb_reg(dm, R_0x6d8, 0xc0, 2);
		}
	}
	p_div->default_tx_path = path;

	PHYDM_DBG(dm, DBG_PATH_DIV, "path=%s\n",
		  (path == BB_PATH_A) ? "A" : "B");
}

void phydm_path_diversity_init_8812a(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	u32 i;

	odm_set_bb_reg(dm, R_0x80c, BIT(29), 1); /* Tx path from Reg */
	odm_set_bb_reg(dm, R_0x80c, 0xFFF0, 0x111); /* Tx by Reg */
	odm_set_bb_reg(dm, R_0x6d8, BIT(7) | BIT6, 1); /* Resp Tx by Txinfo */
	phydm_set_tx_path_by_bb_reg(dm, RF_PATH_A);

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
		p_div->path_sel[i] = 1; /* TxInfo default at path-A */
}
#endif

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
void phydm_set_resp_tx_path_by_fw_jgr3(void *dm_void, u8 macid,
				       enum bb_path path, boolean enable)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	u8 h2c_para[7] = {0};
	u8 path_map[4] = {0}; /* tx logic map*/
	u8 num_enable_path = 0;
	u8 n_tx_path_ctrl_map = 0;
	u8 i = 0, n_sts = 0;

	/*Response TX is controlled in FW ctrl info*/

	PHYDM_DBG(dm, DBG_PATH_DIV, "[%s] =====>\n", __func__);

	if (enable) {
		n_tx_path_ctrl_map = path;

		for (i = 0; i < 4; i++) {
			path_map[i] = 0;
			if (path & BIT(i))
				num_enable_path++;
		}

		for (i = 0; i < 4; i++) {
			if (path & BIT(i)) {
				path_map[i] = n_sts;
				n_sts++;

				if (n_sts == num_enable_path)
					break;
			}
		}
	}

	PHYDM_DBG(dm, DBG_PATH_DIV, "ctrl_map=0x%x Map[D:A]={%d, %d, %d, %d}\n",
		  n_tx_path_ctrl_map,
		  path_map[3], path_map[2], path_map[1], path_map[0]);

	h2c_para[0] = macid;
	h2c_para[1] = n_tx_path_ctrl_map;
	h2c_para[2] = (path_map[3] << 6) | (path_map[2] << 4) |
		      (path_map[1] << 2) | path_map[0];

	odm_fill_h2c_cmd(dm, PHYDM_H2C_DYNAMIC_TX_PATH, 7, h2c_para);
}

void phydm_get_tx_path_txdesc_jgr3(void *dm_void, u8 macid,
				   struct path_txdesc_ctrl *desc)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	u8 ant_map_a = 0, ant_map_b = 0;
	u8 ntx_map = 0;

	if (p_div->path_sel[macid] == BB_PATH_A) {
		desc->ant_map_a = 0; /*offest24[23:22]*/
		desc->ant_map_b = 0; /*offest24[25:24]*/
		desc->ntx_map = BB_PATH_A; /*offest28[23:20]*/
	} else if (p_div->path_sel[macid] == BB_PATH_B) {
		desc->ant_map_a = 0; /*offest24[23:22]*/
		desc->ant_map_b = 0; /*offest24[25:24]*/
		desc->ntx_map = BB_PATH_B; /*offest28[23:20]*/
	} else {
		desc->ant_map_a = 0; /*offest24[23:22]*/
		desc->ant_map_b = 1; /*offest24[25:24]*/
		desc->ntx_map = BB_PATH_AB; /*offest28[23:20]*/
	}
}
#endif

void phydm_tx_path_by_mac_or_reg(void *dm_void, enum phydm_path_ctrl ctrl)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;

	PHYDM_DBG(dm, DBG_PATH_DIV, "[%s] ctrl=%s\n",
		  __func__, (ctrl == TX_PATH_BY_REG) ? "REG" : "DESC");

	if (ctrl == p_div->tx_path_ctrl)
		return;

	p_div->tx_path_ctrl = ctrl;

	switch (dm->support_ic_type) {
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	case ODM_RTL8822B:
	case ODM_RTL8822C:
	case ODM_RTL8812F:
	case ODM_RTL8197G:
		if (ctrl == TX_PATH_BY_REG) {
			odm_set_bb_reg(dm, R_0x1e24, BIT(16), 0); /*OFDM*/
			odm_set_bb_reg(dm, R_0x1a84, 0xe0, 0); /*CCK*/
		} else {
			odm_set_bb_reg(dm, R_0x1e24, BIT(16), 1); /*OFDM*/
			odm_set_bb_reg(dm, R_0x1a84, 0xe0, 7); /*CCK*/
		}

		break;
	#endif
	#if 0 /*(RTL8822B_SUPPORT)*/ /*@ HW Bug*/
	case ODM_RTL8822B:
		if (ctrl == TX_PATH_BY_REG) {
			odm_set_bb_reg(dm, R_0x93c, BIT(18), 0);
			odm_set_bb_reg(dm, R_0xa84, 0xe0, 0); /*CCK*/
		} else {
			odm_set_bb_reg(dm, R_0x93c, BIT(18), 1);
			odm_set_bb_reg(dm, R_0xa84, 0xe0, 7); /*CCK*/
		}

		break;
	#endif
	default:
		break;
	}
}

void phydm_fix_1ss_tx_path_by_bb_reg(void *dm_void,
				     enum bb_path tx_path_sel_1ss,
				     enum bb_path tx_path_sel_cck)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;

	if (tx_path_sel_1ss != BB_PATH_AUTO) {
		p_div->ofdm_fix_path_en = true;
		p_div->ofdm_fix_path_sel = tx_path_sel_1ss;
	} else {
		p_div->ofdm_fix_path_en = false;
		p_div->ofdm_fix_path_sel = dm->tx_1ss_status;
	}

	if (tx_path_sel_cck != BB_PATH_AUTO) {
		p_div->cck_fix_path_en = true;
		p_div->cck_fix_path_sel = tx_path_sel_cck;
	} else {
		p_div->cck_fix_path_en = false;
		p_div->cck_fix_path_sel = dm->tx_1ss_status;
	}

	p_div->force_update = true;

	PHYDM_DBG(dm, DBG_PATH_DIV,
		  "{OFDM_fix_en=%d, path=%d} {CCK_fix_en=%d, path=%d}\n",
		  p_div->ofdm_fix_path_en, p_div->ofdm_fix_path_sel,
		  p_div->cck_fix_path_en, p_div->cck_fix_path_sel);
}

void phydm_set_tx_path_by_bb_reg(void *dm_void, enum bb_path tx_path_sel_1ss)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	enum bb_path tx_path_sel_cck = tx_path_sel_1ss;

	if (!p_div->force_update) {
		if (tx_path_sel_1ss == p_div->default_tx_path) {
			PHYDM_DBG(dm, DBG_PATH_DIV, "Stay in TX path=%s\n",
				  (tx_path_sel_1ss == BB_PATH_A) ? "A" : "B");
			return;
		}
	}
	p_div->force_update = false;

	p_div->default_tx_path = tx_path_sel_1ss;

	PHYDM_DBG(dm, DBG_PATH_DIV, "Switch TX path=%s\n",
		  (tx_path_sel_1ss == BB_PATH_A) ? "A" : "B");

	/*Adv-ctrl mode*/
	if (p_div->cck_fix_path_en) {
		PHYDM_DBG(dm, DBG_PATH_DIV, "Fix CCK TX path=%d\n",
			  p_div->cck_fix_path_sel);
		tx_path_sel_cck = p_div->cck_fix_path_sel;
	}

	if (p_div->ofdm_fix_path_en) {
		PHYDM_DBG(dm, DBG_PATH_DIV, "Fix OFDM TX path=%d\n",
			  p_div->ofdm_fix_path_sel);
		tx_path_sel_1ss = p_div->ofdm_fix_path_sel;
	}

	switch (dm->support_ic_type) {
	#if RTL8822C_SUPPORT
	case ODM_RTL8822C:
		phydm_config_tx_path_8822c(dm, dm->tx_2ss_status,
					   tx_path_sel_1ss, tx_path_sel_cck);
		break;
	#endif

	#if RTL8822B_SUPPORT
	case ODM_RTL8822B:
		if (dm->tx_ant_status != BB_PATH_AB)
			return;

		phydm_config_tx_path_8822b(dm, BB_PATH_AB,
					   tx_path_sel_1ss, tx_path_sel_cck);
		break;
	#endif

	#if RTL8192F_SUPPORT
	case ODM_RTL8192F:
		if (dm->tx_ant_status != BB_PATH_AB)
			return;

		phydm_config_tx_path_8192f(dm, BB_PATH_AB,
					   tx_path_sel_1ss, tx_path_sel_cck);
		break;
	#endif

	#if RTL8812A_SUPPORT
	case ODM_RTL8812:
		phydm_update_tx_path_8812a(dm, tx_path_sel_1ss);
		break;
	#endif
	default:
		break;
	}
}

void phydm_tx_path_diversity_2ss(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	struct cmn_sta_info *sta;
	enum bb_path default_tx_path = BB_PATH_A, path = BB_PATH_A;
	u32 rssi_a = 0, rssi_b = 0;
	u32 local_max_rssi, glb_min_rssi = 0xff;
	u8 i = 0;

	PHYDM_DBG(dm, DBG_PATH_DIV, "[%s] =======>\n", __func__);

	if (!dm->is_linked) {
		if (dm->first_disconnect)
			phydm_tx_path_by_mac_or_reg(dm, TX_PATH_BY_REG);

		PHYDM_DBG(dm, DBG_PATH_DIV, "No Link\n");
		return;
	}

	#if 0/*def PHYDM_IC_JGR3_SERIES_SUPPORT*/
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		if (dm->is_one_entry_only || p_div->cck_fix_path_en ||
		    p_div->ofdm_fix_path_en)
			phydm_tx_path_by_mac_or_reg(dm, TX_PATH_BY_REG);
		else
			phydm_tx_path_by_mac_or_reg(dm, TX_PATH_BY_DESC);
	}
	#endif

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		sta = dm->phydm_sta_info[i];
		if (!is_sta_active(sta))
			continue;

		/* 2 Caculate RSSI per path */
		rssi_a = PHYDM_DIV(p_div->path_a_sum[i], p_div->path_a_cnt[i]);
		rssi_b = PHYDM_DIV(p_div->path_b_sum[i], p_div->path_b_cnt[i]);

		if (rssi_a == rssi_b)
			path =  p_div->default_tx_path;
		else
			path = (rssi_a > rssi_b) ? BB_PATH_A : BB_PATH_B;

		local_max_rssi = (rssi_a > rssi_b) ? rssi_a : rssi_b;

		PHYDM_DBG(dm, DBG_PATH_DIV,
			  "[%d]PathA sum=%d, cnt=%d, avg_rssi=%d\n",
			  i, p_div->path_a_sum[i],
			  p_div->path_a_cnt[i], rssi_a);
		PHYDM_DBG(dm, DBG_PATH_DIV,
			  "[%d]PathB sum=%d, cnt=%d, avg_rssi=%d\n",
			  i, p_div->path_b_sum[i],
			  p_div->path_b_cnt[i], rssi_b);

		/*Select default Tx path */
		if (local_max_rssi < glb_min_rssi) {
			glb_min_rssi = local_max_rssi;
			default_tx_path = path;
		}

		if (p_div->path_sel[i] != path) {
			p_div->path_sel[i] = path;
			#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
			if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
				phydm_set_resp_tx_path_by_fw_jgr3(dm, i,
								  path, true);
			#endif
		}

		p_div->path_a_cnt[i] = 0;
		p_div->path_a_sum[i] = 0;
		p_div->path_b_cnt[i] = 0;
		p_div->path_b_sum[i] = 0;
	}

	/* 2 Update default Tx path */
	phydm_set_tx_path_by_bb_reg(dm, default_tx_path);
	PHYDM_DBG(dm, DBG_PATH_DIV, "[%s] end\n\n", __func__);
}

void phydm_tx_path_diversity(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;

	p_div->path_div_in_progress = false;

	if (!(dm->support_ability & ODM_BB_PATH_DIV))
		return;

	if (p_div->stop_path_div) {
		PHYDM_DBG(dm, DBG_PATH_DIV,
			  "stop_path_div=1, tx_1ss_status=%d\n",
			  dm->tx_1ss_status);
		return;
	}

	switch (dm->support_ic_type) {
	#ifdef PHYDM_CONFIG_PATH_DIV_V2
	case ODM_RTL8822B:
	case ODM_RTL8822C:
	case ODM_RTL8192F:
	case ODM_RTL8812F:
	case ODM_RTL8197G:
		if (dm->rx_ant_status != BB_PATH_AB) {
			PHYDM_DBG(dm, DBG_PATH_DIV,
				  "[Return] tx_Path_en=%d, rx_Path_en=%d\n",
				  dm->tx_ant_status, dm->rx_ant_status);
			return;
		}

		p_div->path_div_in_progress = true;
		phydm_tx_path_diversity_2ss(dm);
		break;
	#endif

	#if (RTL8812A_SUPPORT)
	case ODM_RTL8812:
		phydm_tx_path_diversity_2ss(dm);
		break;
	#endif

	#if RTL8814A_SUPPORT
	case ODM_RTL8814A:
		phydm_dynamic_tx_path(dm);
		break;
	#endif
	}
}

void phydm_tx_path_diversity_init_v2(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	u32 i = 0;

	PHYDM_DBG(dm, DBG_PATH_DIV, "[%s] ====>\n", __func__);

	/*BB_PATH_AB is a invalid value used for init state*/
	p_div->default_tx_path = BB_PATH_A;
	p_div->tx_path_ctrl = TX_PATH_CTRL_INIT;
	p_div->path_div_in_progress = false;

	p_div->cck_fix_path_en = false;
	p_div->ofdm_fix_path_en = false;
	p_div->force_update = false;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
		p_div->path_sel[i] = BB_PATH_A; /* TxInfo default at path-A */

	phydm_tx_path_by_mac_or_reg(dm, TX_PATH_BY_REG);
}

void phydm_tx_path_diversity_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_PATH_DIV))
		return;

	switch (dm->support_ic_type) {
	#ifdef PHYDM_CONFIG_PATH_DIV_V2
	case ODM_RTL8822C:
	case ODM_RTL8822B:
	case ODM_RTL8192F:
	case ODM_RTL8812F:
	case ODM_RTL8197G:
	phydm_tx_path_diversity_init_v2(dm); /*@ After 8822B*/
	break;
	#endif

	#if RTL8812A_SUPPORT
	case ODM_RTL8812:
	phydm_path_diversity_init_8812a(dm);
	break;
	#endif

	#if RTL8814A_SUPPORT
	case ODM_RTL8814A:
	phydm_dynamic_tx_path_init(dm);
	break;
	#endif
	}
}

void phydm_process_rssi_for_path_div(void *dm_void, void *phy_info_void,
				     void *pkt_info_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_phyinfo_struct *phy_info = NULL;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	u8 id = 0;

	phy_info = (struct phydm_phyinfo_struct *)phy_info_void;
	pktinfo = (struct phydm_perpkt_info_struct *)pkt_info_void;

	if (!(pktinfo->is_packet_to_self || pktinfo->is_packet_match_bssid))
		return;

	if (pktinfo->is_cck_rate)
		return;

	id = pktinfo->station_id;
	p_div->path_a_sum[id] += phy_info->rx_mimo_signal_strength[0];
	p_div->path_a_cnt[id]++;

	p_div->path_b_sum[id] += phy_info->rx_mimo_signal_strength[1];
	p_div->path_b_cnt[id]++;
}

void phydm_pathdiv_debug(void *dm_void, char input[][16], u32 *_used,
			 char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;
	char help[] = "-h";
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 val[10] = {0};
	u8 i, input_idx = 0;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_HEX, &val[i]);
			input_idx++;
		}
	}

	if (input_idx == 0)
		return;

	PHYDM_SSCANF(input[1], DCMD_HEX, &val[0]);
	PHYDM_SSCANF(input[2], DCMD_HEX, &val[1]);

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{1:TX Ctrl Sig} {0:BB, 1:MAC}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{2:BB Default TX REG} {path}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{3:MAC DESC TX} {path} {macid}\n");
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{4:MAC Resp TX} {path} {macid}\n");
		#endif
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{5:Fix 1ss path} {ofdm path} {cck path}\n");
	} else if (val[0] == 1) {
		phydm_tx_path_by_mac_or_reg(dm, (enum phydm_path_ctrl)val[1]);
	} else if (val[0] == 2) {
		phydm_set_tx_path_by_bb_reg(dm, (enum bb_path)val[1]);
	} else if (val[0] == 3) {
		p_div->path_sel[val[2]] = (enum bb_path)val[1];
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (val[0] == 4) {
		phydm_set_resp_tx_path_by_fw_jgr3(dm, (u8)val[2],
						  (enum bb_path)val[1], true);
	#endif
	} else if (val[0] == 5) {
		phydm_fix_1ss_tx_path_by_bb_reg(dm, (enum bb_path)val[1],
						(enum bb_path)val[2]);
	}
	*_used = used;
	*_out_len = out_len;
}

void phydm_c2h_dtp_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ODM_PATH_DIVERSITY_ *p_div = &dm->dm_path_div;

	u8 macid = cmd_buf[0];
	u8 target = cmd_buf[1];
	u8 nsc_1 = cmd_buf[2];
	u8 nsc_2 = cmd_buf[3];
	u8 nsc_3 = cmd_buf[4];

	PHYDM_DBG(dm, DBG_PATH_DIV, "Target_candidate = (( %d ))\n", target);
/*@
	if( (nsc_1 >= nsc_2) &&  (nsc_1 >= nsc_3))
	{
		phydm_dtp_fix_tx_path(dm, p_div->ant_candidate_1);
	}
	else	if( nsc_2 >= nsc_3)
	{
		phydm_dtp_fix_tx_path(dm, p_div->ant_candidate_2);
	}
	else
	{
		phydm_dtp_fix_tx_path(dm, p_div->ant_candidate_3);
	}
	*/
}

#endif /*  @#ifdef CONFIG_PATH_DIVERSITY */
