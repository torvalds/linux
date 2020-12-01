/* SPDX-License-Identifier: GPL-2.0 */
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

/*@************************************************************
 * include files
 * ************************************************************
 */

#include "mp_precomp.h"
#include "phydm_precomp.h"

enum channel_width phydm_rxsc_2_bw(void *dm_void, u8 rxsc)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	enum channel_width bw = 0;

	/* @Check RX bandwidth */
	if (rxsc == 0)
		bw = *dm->band_width; /*@full bw*/
	else if (rxsc >= 1 && rxsc <= 8)
		bw = CHANNEL_WIDTH_20;
	else if (rxsc >= 9 && rxsc <= 12)
		bw = CHANNEL_WIDTH_40;
	else /*if (rxsc >= 13)*/
		bw = CHANNEL_WIDTH_80;

	return bw;
}

void phydm_reset_bb_hw_cnt(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	/*@ Reset all counter when 1 */
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		#if (RTL8723F_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8723F) {
			odm_set_bb_reg(dm, R_0x2a44, BIT(21), 0);
			odm_set_bb_reg(dm, R_0x2a44, BIT(21), 1);
		}
		#endif
		odm_set_bb_reg(dm, R_0x1eb4, BIT(25), 1);
		odm_set_bb_reg(dm, R_0x1eb4, BIT(25), 0);
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		/*@ Reset all counter when 1 (including PMAC and PHY)*/
		/* Reset Page F counter*/
		odm_set_bb_reg(dm, R_0xb58, BIT(0), 1);
		odm_set_bb_reg(dm, R_0xb58, BIT(0), 0);
	} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, R_0xf14, BIT(16), 0x1);
		odm_set_bb_reg(dm, R_0xf14, BIT(16), 0x0);
	}
}

void phydm_dynamic_ant_weighting(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

#ifdef DYN_ANT_WEIGHTING_SUPPORT
	#if (RTL8197F_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8197F))
		phydm_dynamic_ant_weighting_8197f(dm);
	#endif

	#if (RTL8812A_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8812)) {
		phydm_dynamic_ant_weighting_8812a(dm);
	}
	#endif

	#if (RTL8822B_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8822B))
		phydm_dynamic_ant_weighting_8822b(dm);
	#endif
#endif
}

#ifdef DYN_ANT_WEIGHTING_SUPPORT
void phydm_ant_weight_dbg(void *dm_void, char input[][16], u32 *_used,
			  char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (!(dm->support_ic_type &
	    (ODM_RTL8192F | ODM_RTL8822B | ODM_RTL8812 | ODM_RTL8197F))) {
		return;
	}

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "echo dis_dym_ant_weighting {0/1}\n");

	} else {
		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (var1[0] == 1) {
			dm->is_disable_dym_ant_weighting = 1;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Disable dyn-ant-weighting\n");
		} else {
			dm->is_disable_dym_ant_weighting = 0;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Enable dyn-ant-weighting\n");
		}
	}
	*_used = used;
	*_out_len = out_len;
}
#endif

void phydm_trx_antenna_setting_init(void *dm_void, u8 num_rf_path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 rx_ant = 0, tx_ant = 0;
	u8 path_bitmap = 1;

	path_bitmap = (u8)phydm_gen_bitmask(num_rf_path);

	/*PHYDM_DBG(dm, ODM_COMP_INIT, "path_bitmap=0x%x\n", path_bitmap);*/

	dm->tx_ant_status = path_bitmap;
	dm->rx_ant_status = path_bitmap;

	if (num_rf_path == PDM_1SS)
		return;

	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	if (dm->support_ic_type &
		   (ODM_RTL8192F | ODM_RTL8192E | ODM_RTL8197F)) {
		dm->rx_ant_status = (u8)odm_get_bb_reg(dm, R_0xc04, 0x3);
		dm->tx_ant_status = (u8)odm_get_bb_reg(dm, R_0x90c, 0x3);
	} else if (dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8814A)) {
		dm->rx_ant_status = (u8)odm_get_bb_reg(dm, R_0x808, 0xf);
		dm->tx_ant_status = (u8)odm_get_bb_reg(dm, R_0x80c, 0xf);
	}
	#endif
	/* @trx_ant_status are already updated in trx mode API in JGR3 ICs */

	PHYDM_DBG(dm, ODM_COMP_INIT, "[%s]ant_status{tx,rx}={0x%x, 0x%x}\n",
		  __func__, dm->tx_ant_status, dm->rx_ant_status);
}

void phydm_config_ofdm_tx_path(void *dm_void, enum bb_path path)
{
#if (RTL8192E_SUPPORT || RTL8192F_SUPPORT || RTL8812A_SUPPORT)
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 ofdm_tx_path = 0x33;

	if (dm->num_rf_path == PDM_1SS)
		return;

	switch (dm->support_ic_type) {
	#if (RTL8192E_SUPPORT || RTL8192F_SUPPORT)
	case ODM_RTL8192E:
	case ODM_RTL8192F:
		if (path == BB_PATH_A)
			odm_set_bb_reg(dm, R_0x90c, MASKDWORD, 0x81121313);
		else if (path == BB_PATH_B)
			odm_set_bb_reg(dm, R_0x90c, MASKDWORD, 0x82221323);
		else if (path == BB_PATH_AB)
			odm_set_bb_reg(dm, R_0x90c, MASKDWORD, 0x83321333);

		break;
	#endif

	#if (RTL8812A_SUPPORT)
	case ODM_RTL8812:
		if (path == BB_PATH_A)
			ofdm_tx_path = 0x11;
		else if (path == BB_PATH_B)
			ofdm_tx_path = 0x22;
		else if (path == BB_PATH_AB)
			ofdm_tx_path = 0x33;

		odm_set_bb_reg(dm, R_0x80c, 0xff00, ofdm_tx_path);

		break;
	#endif

	default:
		break;
	}
#endif
}

void phydm_config_ofdm_rx_path(void *dm_void, enum bb_path path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 val = 0;

	if (dm->support_ic_type & (ODM_RTL8192E | ODM_RTL8192F)) {
#if (RTL8192E_SUPPORT || RTL8192F_SUPPORT)
		if (path == BB_PATH_A)
			val = 1;
		else if (path == BB_PATH_B)
			val = 2;
		else if (path == BB_PATH_AB)
			val = 3;

		odm_set_bb_reg(dm, R_0xc04, 0xff, ((val << 4) | val));
		odm_set_bb_reg(dm, R_0xd04, 0xf, val);
#endif
	}
#if (RTL8812A_SUPPORT || RTL8822B_SUPPORT)
	else if (dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8822B)) {
		if (path == BB_PATH_A)
			val = 1;
		else if (path == BB_PATH_B)
			val = 2;
		else if (path == BB_PATH_AB)
			val = 3;

		odm_set_bb_reg(dm, R_0x808, MASKBYTE0, ((val << 4) | val));
	}
#endif
}

void phydm_config_cck_rx_antenna_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	if (dm->support_ic_type & ODM_IC_1SS)
		return;

	/*@CCK 2R CCA parameters*/
	odm_set_bb_reg(dm, R_0xa00, BIT(15), 0x0); /*@Disable Ant diversity*/
	odm_set_bb_reg(dm, R_0xa70, BIT(7), 0); /*@Concurrent CCA at LSB & USB*/
	odm_set_bb_reg(dm, R_0xa74, BIT(8), 0); /*RX path diversity enable*/
	odm_set_bb_reg(dm, R_0xa14, BIT(7), 0); /*r_en_mrc_antsel*/
	odm_set_bb_reg(dm, R_0xa20, (BIT(5) | BIT(4)), 1); /*@MBC weighting*/

	if (dm->support_ic_type & (ODM_RTL8192E | ODM_RTL8197F | ODM_RTL8192F))
		odm_set_bb_reg(dm, R_0xa08, BIT(28), 1); /*r_cck_2nd_sel_eco*/
	else if (dm->support_ic_type & ODM_RTL8814A)
		odm_set_bb_reg(dm, R_0xa84, BIT(28), 1); /*@2R CCA only*/
#endif
}

void phydm_config_cck_rx_path(void *dm_void, enum bb_path path)
{
#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 path_div_select = 0;
	u8 cck_path[2] = {0};
	u8 en_2R_path = 0;
	u8 en_2R_mrc = 0;
	u8 i = 0, j = 0;
	u8 num_enable_path = 0;
	u8 cck_mrc_max_path = 2;

	if (dm->support_ic_type & ODM_IC_1SS)
		return;

	for (i = 0; i < 4; i++) {
		if (path & BIT(i)) { /*@ex: PHYDM_ABCD*/
			num_enable_path++;
			cck_path[j] = i;
			j++;
		}
		if (num_enable_path >= cck_mrc_max_path)
			break;
	}

	if (num_enable_path > 1) {
		path_div_select = 1;
		en_2R_path = 1;
		en_2R_mrc = 1;
	} else {
		path_div_select = 0;
		en_2R_path = 0;
		en_2R_mrc = 0;
	}
	/*@CCK_1 input signal path*/
	odm_set_bb_reg(dm, R_0xa04, (BIT(27) | BIT(26)), cck_path[0]);
	/*@CCK_2 input signal path*/
	odm_set_bb_reg(dm, R_0xa04, (BIT(25) | BIT(24)), cck_path[1]);
	/*@enable Rx path diversity*/
	odm_set_bb_reg(dm, R_0xa74, BIT(8), path_div_select);
	/*@enable 2R Rx path*/
	odm_set_bb_reg(dm, R_0xa2c, BIT(18), en_2R_path);
	/*@enable 2R MRC*/
	odm_set_bb_reg(dm, R_0xa2c, BIT(22), en_2R_mrc);
	if (dm->support_ic_type & (ODM_RTL8192F | ODM_RTL8197F)) {
		if (path == BB_PATH_A) {
			odm_set_bb_reg(dm, R_0xa04, (BIT(27) | BIT(26)), 0);
			odm_set_bb_reg(dm, R_0xa04, (BIT(25) | BIT(24)), 0);
			odm_set_bb_reg(dm, R_0xa74, BIT(8), 0);
			odm_set_bb_reg(dm, R_0xa2c, (BIT(18) | BIT(17)), 0);
			odm_set_bb_reg(dm, R_0xa2c, (BIT(22) | BIT(21)), 0);
		} else if (path == BB_PATH_B) {/*@for DC cancellation*/
			odm_set_bb_reg(dm, R_0xa04, (BIT(27) | BIT(26)), 1);
			odm_set_bb_reg(dm, R_0xa04, (BIT(25) | BIT(24)), 1);
			odm_set_bb_reg(dm, R_0xa74, BIT(8), 0);
			odm_set_bb_reg(dm, R_0xa2c, (BIT(18) | BIT(17)), 0);
			odm_set_bb_reg(dm, R_0xa2c, (BIT(22) | BIT(21)), 0);
		} else if (path == BB_PATH_AB) {
			odm_set_bb_reg(dm, R_0xa04, (BIT(27) | BIT(26)), 0);
			odm_set_bb_reg(dm, R_0xa04, (BIT(25) | BIT(24)), 1);
			odm_set_bb_reg(dm, R_0xa74, BIT(8), 1);
			odm_set_bb_reg(dm, R_0xa2c, (BIT(18) | BIT(17)), 1);
			odm_set_bb_reg(dm, R_0xa2c, (BIT(22) | BIT(21)), 1);
		}
	} else if (dm->support_ic_type & ODM_RTL8822B) {
		if (path == BB_PATH_A) {
			odm_set_bb_reg(dm, R_0xa04, (BIT(27) | BIT(26)), 0);
			odm_set_bb_reg(dm, R_0xa04, (BIT(25) | BIT(24)), 0);
		} else {
			odm_set_bb_reg(dm, R_0xa04, (BIT(27) | BIT(26)), 1);
			odm_set_bb_reg(dm, R_0xa04, (BIT(25) | BIT(24)), 1);
		}
	}

#endif
}

void phydm_config_cck_tx_path(void *dm_void, enum bb_path path)
{
#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (path == BB_PATH_A)
		odm_set_bb_reg(dm, R_0xa04, 0xf0000000, 0x8);
	else if (path == BB_PATH_B)
		odm_set_bb_reg(dm, R_0xa04, 0xf0000000, 0x4);
	else /*if (path == BB_PATH_AB)*/
		odm_set_bb_reg(dm, R_0xa04, 0xf0000000, 0xc);
#endif
}

void phydm_config_trx_path_v2(void *dm_void, char input[][16], u32 *_used,
			      char *output, u32 *_out_len)
{
#if (RTL8822B_SUPPORT || RTL8197F_SUPPORT || RTL8192F_SUPPORT ||\
	RTL8822C_SUPPORT || RTL8814B_SUPPORT || RTL8197G_SUPPORT ||\
	RTL8812F_SUPPORT || RTL8198F_SUPPORT)
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 val[10] = {0};
	char help[] = "-h";
	u8 i = 0, input_idx = 0;
	enum bb_path tx_path, rx_path, tx_path_ctrl;
	boolean dbg_mode_en;

	if (!(dm->support_ic_type &
	    (ODM_RTL8822B | ODM_RTL8197F | ODM_RTL8192F | ODM_RTL8822C |
	     ODM_RTL8814B | ODM_RTL8812F | ODM_RTL8197G | ODM_RTL8198F)))
		return;

	for (i = 0; i < 5; i++) {
		PHYDM_SSCANF(input[i + 1], DCMD_HEX, &val[i]);
		input_idx++;
	}

	if (input_idx == 0)
		return;

	dbg_mode_en = (boolean)val[0];
	tx_path = (enum bb_path)val[1];
	rx_path = (enum bb_path)val[2];
	tx_path_ctrl = (enum bb_path)val[3];

	if ((strcmp(input[1], help) == 0)) {
		if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8822B |
					   ODM_RTL8192F)) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "{en} {tx_path} {rx_path} {ff:auto, else:1ss_tx_path}\n"
				 );
		} else {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "{en} {tx_path} {rx_path} {is_tx_2_path}\n");
		}

	} else if (dbg_mode_en) {
		dm->is_disable_phy_api = false;
		phydm_api_trx_mode(dm, tx_path, rx_path, tx_path_ctrl);
		dm->is_disable_phy_api = true;
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "T/RX path = 0x%x/0x%x, tx_path_ctrl=%d\n",
			 tx_path, rx_path, tx_path_ctrl);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "T/RX path_en={0x%x, 0x%x}, tx_1ss=%d\n",
			 dm->tx_ant_status, dm->rx_ant_status,
			 dm->tx_1ss_status);
	} else {
		dm->is_disable_phy_api = false;
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Disable API debug mode\n");
	}
#endif
}

void phydm_config_trx_path_v1(void *dm_void, char input[][16], u32 *_used,
			      char *output, u32 *_out_len)
{
#if (RTL8192E_SUPPORT || RTL8812A_SUPPORT)
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 val[10] = {0};
	char help[] = "-h";
	u8 i = 0, input_idx = 0;

	if (!(dm->support_ic_type & (ODM_RTL8192E | ODM_RTL8812)))
		return;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_HEX, &val[i]);
			input_idx++;
		}
	}

	if (input_idx == 0)
		return;

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{0:CCK, 1:OFDM} {1:TX, 2:RX} {1:path_A, 2:path_B, 3:path_AB}\n");

		*_used = used;
		*_out_len = out_len;
		return;

	} else if (val[0] == 0) {
	/* @CCK */
		if (val[1] == 1) { /*TX*/
			if (val[2] == 1)
				phydm_config_cck_tx_path(dm, BB_PATH_A);
			else if (val[2] == 2)
				phydm_config_cck_tx_path(dm, BB_PATH_B);
			else if (val[2] == 3)
				phydm_config_cck_tx_path(dm, BB_PATH_AB);
		} else if (val[1] == 2) { /*RX*/

			phydm_config_cck_rx_antenna_init(dm);

			if (val[2] == 1)
				phydm_config_cck_rx_path(dm, BB_PATH_A);
			else if (val[2] == 2)
				phydm_config_cck_rx_path(dm, BB_PATH_B);
			else if (val[2] == 3)
				phydm_config_cck_rx_path(dm, BB_PATH_AB);
			}
		}
	/* OFDM */
	else if (val[0] == 1) {
		if (val[1] == 1) /*TX*/
			phydm_config_ofdm_tx_path(dm, val[2]);
		else if (val[1] == 2) /*RX*/
			phydm_config_ofdm_rx_path(dm, val[2]);
	}

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "PHYDM Set path [%s] [%s] = [%s%s%s%s]\n",
		 (val[0] == 1) ? "OFDM" : "CCK",
		 (val[1] == 1) ? "TX" : "RX",
		 (val[2] & 0x1) ? "A" : "", (val[2] & 0x2) ? "B" : "",
		 (val[2] & 0x4) ? "C" : "",
		 (val[2] & 0x8) ? "D" : "");

	*_used = used;
	*_out_len = out_len;
#endif
}

void phydm_config_trx_path(void *dm_void, char input[][16], u32 *_used,
			   char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & (ODM_RTL8192E | ODM_RTL8812)) {
		#if (RTL8192E_SUPPORT || RTL8812A_SUPPORT)
		phydm_config_trx_path_v1(dm, input, _used, output, _out_len);
		#endif
	} else if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F |
		   ODM_RTL8192F | ODM_RTL8822C | ODM_RTL8812F |
		   ODM_RTL8197G | ODM_RTL8814B | ODM_RTL8198F)) {
		#if (RTL8822B_SUPPORT || RTL8197F_SUPPORT ||\
		     RTL8192F_SUPPORT || RTL8822C_SUPPORT ||\
		     RTL8814B_SUPPORT || RTL8812F_SUPPORT ||\
		     RTL8197G_SUPPORT || RTL8198F_SUPPORT)
		phydm_config_trx_path_v2(dm, input, _used, output, _out_len);
		#endif
	}
}

void phydm_tx_2path(void *dm_void)
{
#if (defined(PHYDM_COMPILE_IC_2SS))
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	enum bb_path rx_path = (enum bb_path)dm->rx_ant_status;

	PHYDM_DBG(dm, ODM_COMP_API, "%s ======>\n", __func__);


	if (!(dm->support_ic_type & ODM_IC_2SS))
		return;

	#if (RTL8822B_SUPPORT || RTL8192F_SUPPORT || RTL8197F_SUPPORT ||\
	     RTL8822C_SUPPORT || RTL8812F_SUPPORT || RTL8197G_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F | ODM_RTL8192F |
	    ODM_RTL8822C | ODM_RTL8812F | ODM_RTL8197G))
		phydm_api_trx_mode(dm, BB_PATH_AB, rx_path, BB_PATH_AB);
	#endif

	#if (RTL8812A_SUPPORT || RTL8192E_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8192E)) {
		phydm_config_cck_tx_path(dm, BB_PATH_AB);
		phydm_config_ofdm_tx_path(dm, BB_PATH_AB);
	}
	#endif
#endif
}

void phydm_stop_3_wire(void *dm_void, u8 set_type)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (set_type == PHYDM_SET) {
		/*@[Stop 3-wires]*/
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			odm_set_bb_reg(dm, R_0x180c, 0x3, 0x0);
			odm_set_bb_reg(dm, R_0x180c, BIT(28), 0x1);

			#if (defined(PHYDM_COMPILE_ABOVE_2SS))
			if (dm->support_ic_type & PHYDM_IC_ABOVE_2SS) {
				odm_set_bb_reg(dm, R_0x410c, 0x3, 0x0);
				odm_set_bb_reg(dm, R_0x410c, BIT(28), 0x1);
			}
			#endif

			#if (defined(PHYDM_COMPILE_ABOVE_4SS))
			if (dm->support_ic_type & PHYDM_IC_ABOVE_4SS) {
				odm_set_bb_reg(dm, R_0x520c, 0x3, 0x0);
				odm_set_bb_reg(dm, R_0x520c, BIT(28), 0x1);
				odm_set_bb_reg(dm, R_0x530c, 0x3, 0x0);
				odm_set_bb_reg(dm, R_0x530c, BIT(28), 0x1);
			}
			#endif
		} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
			odm_set_bb_reg(dm, R_0xc00, 0xf, 0x4);
			odm_set_bb_reg(dm, R_0xe00, 0xf, 0x4);
		} else {
			odm_set_bb_reg(dm, R_0x88c, 0xf00000, 0xf);
		}

	} else { /*@if (set_type == PHYDM_REVERT)*/

		/*@[Start 3-wires]*/
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			odm_set_bb_reg(dm, R_0x180c, 0x3, 0x3);
			odm_set_bb_reg(dm, R_0x180c, BIT(28), 0x1);

			#if (defined(PHYDM_COMPILE_ABOVE_2SS))
			if (dm->support_ic_type & PHYDM_IC_ABOVE_2SS) {
				odm_set_bb_reg(dm, R_0x410c, 0x3, 0x3);
				odm_set_bb_reg(dm, R_0x410c, BIT(28), 0x1);
			}
			#endif

			#if (defined(PHYDM_COMPILE_ABOVE_4SS))
			if (dm->support_ic_type & PHYDM_IC_ABOVE_4SS) {
				odm_set_bb_reg(dm, R_0x520c, 0x3, 0x3);
				odm_set_bb_reg(dm, R_0x520c, BIT(28), 0x1);
				odm_set_bb_reg(dm, R_0x530c, 0x3, 0x3);
				odm_set_bb_reg(dm, R_0x530c, BIT(28), 0x1);
			}
			#endif
		} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
			odm_set_bb_reg(dm, R_0xc00, 0xf, 0x7);
			odm_set_bb_reg(dm, R_0xe00, 0xf, 0x7);
		} else {
			odm_set_bb_reg(dm, R_0x88c, 0xf00000, 0x0);
		}
	}
}

u8 phydm_stop_ic_trx(void *dm_void, u8 set_type)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_api_stuc *api = &dm->api_table;
	u8 i = 0;
	boolean trx_idle_success = false;
	u32 dbg_port_value = 0;

	if (set_type == PHYDM_SET) {
	/*[Stop TRX]---------------------------------------------------------*/
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			#if (RTL8723F_SUPPORT)
			/*Judy 2020-0515*/
			/*set debug port to 0x0*/
			if (!phydm_set_bb_dbg_port(dm, DBGPORT_PRI_3, 0x0))
				return PHYDM_SET_FAIL;
			#endif
			for (i = 0; i < 100; i++) {
				dbg_port_value = odm_get_bb_reg(dm, R_0x2db4,
								MASKDWORD);
				/* BB idle */
				if ((dbg_port_value & 0x1FFEFF3F) == 0 &&
				    (dbg_port_value & 0xC0010000) ==
				    0xC0010000) {
					PHYDM_DBG(dm, ODM_COMP_API,
						  "Stop trx wait for (%d) times\n",
						  i);

					trx_idle_success = true;
					break;
				}
			}
		} else {
			/*set debug port to 0x0*/
			if (!phydm_set_bb_dbg_port(dm, DBGPORT_PRI_3, 0x0))
				return PHYDM_SET_FAIL;
			for (i = 0; i < 100; i++) {
				dbg_port_value = phydm_get_bb_dbg_port_val(dm);
				/* PHYTXON && CCA_all */
				if (dm->support_ic_type & (ODM_RTL8721D |
					ODM_RTL8710B | ODM_RTL8710C |
					ODM_RTL8188F | ODM_RTL8723D)) {
					if ((dbg_port_value &
					    (BIT(20) | BIT(15))) == 0) {
						PHYDM_DBG(dm, ODM_COMP_API,
							  "Stop trx wait for (%d) times\n",
							  i);

						trx_idle_success = true;
						break;
					}
				} else {
					if ((dbg_port_value &
					    (BIT(17) | BIT(3))) == 0) {
						PHYDM_DBG(dm, ODM_COMP_API,
							  "Stop trx wait for (%d) times\n",
							  i);

						trx_idle_success = true;
						break;
					}
				}
				ODM_delay_ms(1);
			}
			phydm_release_bb_dbg_port(dm);
		}

		if (trx_idle_success) {
			api->tx_queue_bitmap = odm_read_1byte(dm, R_0x522);

			/*pause all TX queue*/
			odm_set_mac_reg(dm, R_0x520, 0xff0000, 0xff);

			if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
				/*disable OFDM RX CCA*/
				odm_set_bb_reg(dm, R_0x1d58, 0xff8, 0x1ff);
			} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
				/*disable OFDM RX CCA*/
				odm_set_bb_reg(dm, R_0x838, BIT(1), 1);
			} else {
				api->rxiqc_reg1 = odm_read_4byte(dm, R_0xc14);
				api->rxiqc_reg2 = odm_read_4byte(dm, R_0xc1c);
				/* [ Set IQK Matrix = 0 ]
				 * equivalent to [ Turn off CCA]
				 */
				odm_set_bb_reg(dm, R_0xc14, MASKDWORD, 0x0);
				odm_set_bb_reg(dm, R_0xc1c, MASKDWORD, 0x0);
			}
			phydm_dis_cck_trx(dm, PHYDM_SET);
		} else {
			return PHYDM_SET_FAIL;
		}

		return PHYDM_SET_SUCCESS;

	} else { /*@if (set_type == PHYDM_REVERT)*/
		/*Release all TX queue*/
		odm_write_1byte(dm, R_0x522, api->tx_queue_bitmap);

		if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			/*@enable OFDM RX CCA*/
			odm_set_bb_reg(dm, R_0x1d58, 0xff8, 0x0);
		} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
			/*@enable OFDM RX CCA*/
			odm_set_bb_reg(dm, R_0x838, BIT(1), 0);
		} else {
			/* @[Set IQK Matrix = 0] equivalent to [ Turn off CCA]*/
			odm_write_4byte(dm, R_0xc14, api->rxiqc_reg1);
			odm_write_4byte(dm, R_0xc1c, api->rxiqc_reg2);
		}
		phydm_dis_cck_trx(dm, PHYDM_REVERT);
		return PHYDM_SET_SUCCESS;
	}
}

void phydm_dis_cck_trx(void *dm_void, u8 set_type)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_api_stuc *api = &dm->api_table;

	if (set_type == PHYDM_SET) {
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			if(dm->support_ic_type & ODM_RTL8723F) {
				api->ccktx_path = 1;
				/* @disable CCK CCA */
				odm_set_bb_reg(dm, R_0x2a24, BIT(13), 0x1);
				/* @disable CCK Tx */
				odm_set_bb_reg(dm, R_0x2a00, BIT(1), 0x1);
			} else {
				api->ccktx_path = (u8)odm_get_bb_reg(dm, R_0x1a04,
							     	0xf0000000);
				/* @CCK RxIQ weighting = [0,0] */
				odm_set_bb_reg(dm, R_0x1a14, 0x300, 0x3);
				/* @disable CCK Tx */
				odm_set_bb_reg(dm, R_0x1a04, 0xf0000000, 0x0);
			}
		} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
			api->ccktx_path = (u8)odm_get_bb_reg(dm, R_0xa04,
							     0xf0000000);
			/* @disable CCK block */
			odm_set_bb_reg(dm, R_0x808, BIT(28), 0);
			/* @disable CCK Tx */
			odm_set_bb_reg(dm, R_0xa04, 0xf0000000, 0x0);
		} else {
			api->ccktx_path = (u8)odm_get_bb_reg(dm, R_0xa04,
							     0xf0000000);
			/* @disable whole CCK block */
			odm_set_bb_reg(dm, R_0x800, BIT(24), 0);
			/* @disable CCK Tx */
			odm_set_bb_reg(dm, R_0xa04, 0xf0000000, 0x0);
		}
	} else if (set_type == PHYDM_REVERT) {
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			if(dm->support_ic_type & ODM_RTL8723F) {
				/* @enable CCK CCA */
				odm_set_bb_reg(dm, R_0x2a24, BIT(13), 0x0);
				/* @enable CCK Tx */
				odm_set_bb_reg(dm, R_0x2a00, BIT(1), 0x0);
			} else {
				/* @CCK RxIQ weighting = [1,1] */
				odm_set_bb_reg(dm, R_0x1a14, 0x300, 0x0);
				/* @enable CCK Tx */
				odm_set_bb_reg(dm, R_0x1a04, 0xf0000000,
				       	api->ccktx_path);
			}
		} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
			/* @enable CCK block */
			odm_set_bb_reg(dm, R_0x808, BIT(28), 1);
			/* @enable CCK Tx */
			odm_set_bb_reg(dm, R_0xa04, 0xf0000000,
				       api->ccktx_path);
		} else {
			/* @enable whole CCK block */
			odm_set_bb_reg(dm, R_0x800, BIT(24), 1);
			/* @enable CCK Tx */
			odm_set_bb_reg(dm, R_0xa04, 0xf0000000,
				       api->ccktx_path);
		}
	}
}

void phydm_bw_fixed_enable(void *dm_void, u8 enable)
{
#ifdef CONFIG_BW_INDICATION
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean val = (enable == FUNC_ENABLE) ? 1 : 0;

	if (dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8822B | ODM_RTL8195B))
		odm_set_bb_reg(dm, R_0x840, BIT(4), val);
	else if (dm->support_ic_type & ODM_RTL8822C)
		odm_set_bb_reg(dm, R_0x878, BIT(28), val);
#endif
}

void phydm_bw_fixed_setting(void *dm_void)
{
#ifdef CONFIG_BW_INDICATION
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_api_stuc *api = &dm->api_table;
	u8 bw = *dm->band_width;
	u32 reg = 0, reg_mask = 0, reg_value = 0;

	if (!(dm->support_ic_type & ODM_DYM_BW_INDICATION_SUPPORT))
		return;

	if (dm->support_ic_type & (ODM_RTL8821C | ODM_RTL8822B |
	    ODM_RTL8195B)) {
		reg = R_0x840;
		reg_mask = 0xf;
		reg_value = api->pri_ch_idx;
	} else if (dm->support_ic_type & ODM_RTL8822C) {
		reg = R_0x878;
		reg_mask = 0xc0000000;
		reg_value = 0x0;
	}

	switch (bw) {
	case CHANNEL_WIDTH_80:
		odm_set_bb_reg(dm, reg, reg_mask, reg_value);
		break;
	case CHANNEL_WIDTH_40:
		odm_set_bb_reg(dm, reg, reg_mask, reg_value);
		break;
	default:
		odm_set_bb_reg(dm, reg, reg_mask, 0x0);
	}

	phydm_bw_fixed_enable(dm, FUNC_ENABLE);
#endif
}

void phydm_set_ext_switch(void *dm_void, u32 ext_ant_switch)
{
#if (RTL8821A_SUPPORT || RTL8881A_SUPPORT)
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ic_type & (ODM_RTL8821 | ODM_RTL8881A)))
		return;

	/*Output Pin Settings*/

	/*select DPDT_P and DPDT_N as output pin*/
	odm_set_mac_reg(dm, R_0x4c, BIT(23), 0);

	/*@by WLAN control*/
	odm_set_mac_reg(dm, R_0x4c, BIT(24), 1);

	/*@DPDT_N = 1b'0*/ /*@DPDT_P = 1b'0*/
	odm_set_bb_reg(dm, R_0xcb4, 0xFF, 77);

	if (ext_ant_switch == 1) { /*@2b'01*/
		odm_set_bb_reg(dm, R_0xcb4, (BIT(29) | BIT(28)), 1);
		PHYDM_DBG(dm, ODM_COMP_API, "8821A ant swh=2b'01\n");
	} else if (ext_ant_switch == 2) { /*@2b'10*/
		odm_set_bb_reg(dm, R_0xcb4, BIT(29) | BIT(28), 2);
		PHYDM_DBG(dm, ODM_COMP_API, "*8821A ant swh=2b'10\n");
	}
#endif
}

void phydm_csi_mask_enable(void *dm_void, u32 enable)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean en = false;

	en = (enable == FUNC_ENABLE) ? true : false;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, R_0xd2c, BIT(28), en);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "Enable CSI Mask:  Reg 0xD2C[28] = ((0x%x))\n", en);
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		odm_set_bb_reg(dm, R_0xc0c, BIT(3), en);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "Enable CSI Mask:  Reg 0xc0c[3] = ((0x%x))\n", en);
	#endif
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, R_0x874, BIT(0), en);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "Enable CSI Mask:  Reg 0x874[0] = ((0x%x))\n", en);
	}
}

void phydm_clean_all_csi_mask(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, R_0xd40, MASKDWORD, 0);
		odm_set_bb_reg(dm, R_0xd44, MASKDWORD, 0);
		odm_set_bb_reg(dm, R_0xd48, MASKDWORD, 0);
		odm_set_bb_reg(dm, R_0xd4c, MASKDWORD, 0);
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		u8 i = 0, idx_lmt = 0;

		if (dm->support_ic_type &
		   (ODM_RTL8822C | ODM_RTL8812F | ODM_RTL8197G))
			idx_lmt = 127;
		else /*@for IC supporting 80 + 80*/
			idx_lmt = 255;

		odm_set_bb_reg(dm, R_0x1ee8, 0x3, 0x3);
		odm_set_bb_reg(dm, R_0x1d94, BIT(31) | BIT(30), 0x1);
		for (i = 0; i < idx_lmt; i++) {
			odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2, i);
			odm_set_bb_reg(dm, R_0x1d94, MASKBYTE0, 0x0);
		}
		odm_set_bb_reg(dm, R_0x1ee8, 0x3, 0x0);
	#endif
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, R_0x880, MASKDWORD, 0);
		odm_set_bb_reg(dm, R_0x884, MASKDWORD, 0);
		odm_set_bb_reg(dm, R_0x888, MASKDWORD, 0);
		odm_set_bb_reg(dm, R_0x88c, MASKDWORD, 0);
		odm_set_bb_reg(dm, R_0x890, MASKDWORD, 0);
		odm_set_bb_reg(dm, R_0x894, MASKDWORD, 0);
		odm_set_bb_reg(dm, R_0x898, MASKDWORD, 0);
		odm_set_bb_reg(dm, R_0x89c, MASKDWORD, 0);
	}
}

void phydm_set_csi_mask(void *dm_void, u32 tone_idx_tmp, u8 tone_direction)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 byte_offset = 0, bit_offset = 0;
	u32 target_reg = 0;
	u8 reg_tmp_value = 0;
	u32 tone_num = 64;
	u32 tone_num_shift = 0;
	u32 csi_mask_reg_p = 0, csi_mask_reg_n = 0;

	/* @calculate real tone idx*/
	if ((tone_idx_tmp % 10) >= 5)
		tone_idx_tmp += 10;

	tone_idx_tmp = (tone_idx_tmp / 10);

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		tone_num = 64;
		csi_mask_reg_p = 0xD40;
		csi_mask_reg_n = 0xD48;

	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		tone_num = 128;
		csi_mask_reg_p = 0x880;
		csi_mask_reg_n = 0x890;
	}

	if (tone_direction == FREQ_POSITIVE) {
		if (tone_idx_tmp >= (tone_num - 1))
			tone_idx_tmp = (tone_num - 1);

		byte_offset = (u8)(tone_idx_tmp >> 3);
		bit_offset = (u8)(tone_idx_tmp & 0x7);
		target_reg = csi_mask_reg_p + byte_offset;

	} else {
		tone_num_shift = tone_num;

		if (tone_idx_tmp >= tone_num)
			tone_idx_tmp = tone_num;

		tone_idx_tmp = tone_num - tone_idx_tmp;

		byte_offset = (u8)(tone_idx_tmp >> 3);
		bit_offset = (u8)(tone_idx_tmp & 0x7);
		target_reg = csi_mask_reg_n + byte_offset;
	}

	reg_tmp_value = odm_read_1byte(dm, target_reg);
	PHYDM_DBG(dm, ODM_COMP_API,
		  "Pre Mask tone idx[%d]:  Reg0x%x = ((0x%x))\n",
		  (tone_idx_tmp + tone_num_shift), target_reg, reg_tmp_value);
	reg_tmp_value |= BIT(bit_offset);
	odm_write_1byte(dm, target_reg, reg_tmp_value);
	PHYDM_DBG(dm, ODM_COMP_API,
		  "New Mask tone idx[%d]:  Reg0x%x = ((0x%x))\n",
		  (tone_idx_tmp + tone_num_shift), target_reg, reg_tmp_value);
}

void phydm_set_nbi_reg(void *dm_void, u32 tone_idx_tmp, u32 bw)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	/*tone_idx X 10*/
	u32 nbi_128[NBI_128TONE] = {25, 55, 85, 115, 135,
				    155, 185, 205, 225, 245,
				    265, 285, 305, 335, 355,
				    375, 395, 415, 435, 455,
				    485, 505, 525, 555, 585, 615, 635};
	/*tone_idx X 10*/
	u32 nbi_256[NBI_256TONE] = {25, 55, 85, 115, 135,
				    155, 175, 195, 225, 245,
				    265, 285, 305, 325, 345,
				    365, 385, 405, 425, 445,
				    465, 485, 505, 525, 545,
				    565, 585, 605, 625, 645,
				    665, 695, 715, 735, 755,
				    775, 795, 815, 835, 855,
				    875, 895, 915, 935, 955,
				    975, 995, 1015, 1035, 1055,
				    1085, 1105, 1125, 1145, 1175,
				    1195, 1225, 1255, 1275};
	u32 reg_idx = 0;
	u32 i;
	u8 nbi_table_idx = FFT_128_TYPE;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		nbi_table_idx = FFT_128_TYPE;
	} else if (dm->support_ic_type & ODM_IC_11AC_1_SERIES) {
		nbi_table_idx = FFT_256_TYPE;
	} else if (dm->support_ic_type & ODM_IC_11AC_2_SERIES) {
		if (bw == 80)
			nbi_table_idx = FFT_256_TYPE;
		else /*@20M, 40M*/
			nbi_table_idx = FFT_128_TYPE;
	}

	if (nbi_table_idx == FFT_128_TYPE) {
		for (i = 0; i < NBI_128TONE; i++) {
			if (tone_idx_tmp < nbi_128[i]) {
				reg_idx = i + 1;
				break;
			}
		}

	} else if (nbi_table_idx == FFT_256_TYPE) {
		for (i = 0; i < NBI_256TONE; i++) {
			if (tone_idx_tmp < nbi_256[i]) {
				reg_idx = i + 1;
				break;
			}
		}
	}

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, R_0xc40, 0x1f000000, reg_idx);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "Set tone idx:  Reg0xC40[28:24] = ((0x%x))\n",
			  reg_idx);
	} else {
		odm_set_bb_reg(dm, R_0x87c, 0xfc000, reg_idx);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "Set tone idx: Reg0x87C[19:14] = ((0x%x))\n",
			  reg_idx);
	}
}

void phydm_nbi_enable(void *dm_void, u32 enable)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 val = 0;

	val = (enable == FUNC_ENABLE) ? 1 : 0;

	PHYDM_DBG(dm, ODM_COMP_API, "Enable NBI=%d\n", val);

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		if (dm->support_ic_type & (ODM_RTL8192F | ODM_RTL8197F)) {
			val = (enable == FUNC_ENABLE) ? 0xf : 0;
			odm_set_bb_reg(dm, R_0xc50, 0xf000000, val);
		} else {
			odm_set_bb_reg(dm, R_0xc40, BIT(9), val);
		}
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C |
		    ODM_RTL8195B)) {
			odm_set_bb_reg(dm, R_0x87c, BIT(13), val);
			odm_set_bb_reg(dm, R_0xc20, BIT(28), val);
			if (dm->rf_type > RF_1T1R)
				odm_set_bb_reg(dm, R_0xe20, BIT(28), val);
		} else {
			odm_set_bb_reg(dm, R_0x87c, BIT(13), val);
		}
	}
}

u8 phydm_find_fc(void *dm_void, u32 channel, u32 bw, u32 second_ch, u32 *fc_in)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 fc = *fc_in;
	u32 start_ch_per_40m[NUM_START_CH_40M] = {36, 44, 52, 60, 100,
						  108, 116, 124, 132, 140,
						  149, 157, 165, 173};
	u32 start_ch_per_80m[NUM_START_CH_80M] = {36, 52, 100, 116, 132,
						  149, 165};
	u32 *start_ch = &start_ch_per_40m[0];
	u32 num_start_channel = NUM_START_CH_40M;
	u32 channel_offset = 0;
	u32 i;

	/*@2.4G*/
	if (channel <= 14 && channel > 0) {
		if (bw == 80)
			return PHYDM_SET_FAIL;

		fc = 2412 + (channel - 1) * 5;

		if (bw == 40 && second_ch == PHYDM_ABOVE) {
			if (channel >= 10) {
				PHYDM_DBG(dm, ODM_COMP_API,
					  "CH = ((%d)), Scnd_CH = ((%d)) Error setting\n",
					  channel, second_ch);
				return PHYDM_SET_FAIL;
			}
			fc += 10;
		} else if (bw == 40 && (second_ch == PHYDM_BELOW)) {
			if (channel <= 2) {
				PHYDM_DBG(dm, ODM_COMP_API,
					  "CH = ((%d)), Scnd_CH = ((%d)) Error setting\n",
					  channel, second_ch);
				return PHYDM_SET_FAIL;
			}
			fc -= 10;
		}
	}
	/*@5G*/
	else if (channel >= 36 && channel <= 177) {
		if (bw != 20) {
			if (bw == 40) {
				num_start_channel = NUM_START_CH_40M;
				start_ch = &start_ch_per_40m[0];
				channel_offset = CH_OFFSET_40M;
			} else if (bw == 80) {
				num_start_channel = NUM_START_CH_80M;
				start_ch = &start_ch_per_80m[0];
				channel_offset = CH_OFFSET_80M;
			}

			for (i = 0; i < (num_start_channel - 1); i++) {
				if (channel < start_ch[i + 1]) {
					channel = start_ch[i] + channel_offset;
					break;
				}
			}
			PHYDM_DBG(dm, ODM_COMP_API, "Mod_CH = ((%d))\n",
				  channel);
		}

		fc = 5180 + (channel - 36) * 5;

	} else {
		PHYDM_DBG(dm, ODM_COMP_API, "CH = ((%d)) Error setting\n",
			  channel);
		return PHYDM_SET_FAIL;
	}

	*fc_in = fc;

	return PHYDM_SET_SUCCESS;
}

u8 phydm_find_intf_distance(void *dm_void, u32 bw, u32 fc, u32 f_interference,
			    u32 *tone_idx_tmp_in)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 bw_up = 0, bw_low = 0;
	u32 int_distance = 0;
	u32 tone_idx_tmp = 0;
	u8 set_result = PHYDM_SET_NO_NEED;

	bw_up = fc + bw / 2;
	bw_low = fc - bw / 2;

	PHYDM_DBG(dm, ODM_COMP_API,
		  "[f_l, fc, fh] = [ %d, %d, %d ], f_int = ((%d))\n", bw_low,
		  fc, bw_up, f_interference);

	if (f_interference >= bw_low && f_interference <= bw_up) {
		int_distance = DIFF_2(fc, f_interference);
		/*@10*(int_distance /0.3125)*/
		tone_idx_tmp = (int_distance << 5);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "int_distance = ((%d MHz)) Mhz, tone_idx_tmp = ((%d.%d))\n",
			  int_distance, tone_idx_tmp / 10,
			  tone_idx_tmp % 10);
		*tone_idx_tmp_in = tone_idx_tmp;
		set_result = PHYDM_SET_SUCCESS;
	}

	return set_result;
}

u8 phydm_csi_mask_setting(void *dm_void, u32 enable, u32 ch, u32 bw,
			  u32 f_intf, u32 sec_ch)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 fc = 2412;
	u8 direction = FREQ_POSITIVE;
	u32 tone_idx = 0;
	u8 set_result = PHYDM_SET_SUCCESS;
	u8 rpt = 0;

	if (enable == FUNC_DISABLE) {
		set_result = PHYDM_SET_SUCCESS;
		phydm_clean_all_csi_mask(dm);

	} else {
		PHYDM_DBG(dm, ODM_COMP_API,
			  "[Set CSI MASK_] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
			  ch, bw, f_intf,
			  (((bw == 20) || (ch > 14)) ? "Don't care" :
			  (sec_ch == PHYDM_ABOVE) ? "H" : "L"));

		/*@calculate fc*/
		if (phydm_find_fc(dm, ch, bw, sec_ch, &fc) == PHYDM_SET_FAIL) {
			set_result = PHYDM_SET_FAIL;
		} else {
			/*@calculate interference distance*/
			rpt = phydm_find_intf_distance(dm, bw, fc, f_intf,
						       &tone_idx);
			if (rpt == PHYDM_SET_SUCCESS) {
				if (f_intf >= fc)
					direction = FREQ_POSITIVE;
				else
					direction = FREQ_NEGATIVE;

				phydm_set_csi_mask(dm, tone_idx, direction);
				set_result = PHYDM_SET_SUCCESS;
			} else {
				set_result = PHYDM_SET_NO_NEED;
			}
		}
	}

	if (set_result == PHYDM_SET_SUCCESS)
		phydm_csi_mask_enable(dm, enable);
	else
		phydm_csi_mask_enable(dm, FUNC_DISABLE);

	return set_result;
}

boolean phydm_spur_case_mapping(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 channel = *dm->channel, bw = *dm->band_width;
	boolean mapping_result = false;
#if (RTL8814B_SUPPORT == 1)
	if (channel == 153 && bw == CHANNEL_WIDTH_20) {
		odm_set_bb_reg(dm, R_0x804, BIT(31), 0);
		odm_set_bb_reg(dm, R_0xc00, BIT(25) | BIT(24), 0);
		mapping_result =  true;
	} else if (channel == 151 && bw == CHANNEL_WIDTH_40) {
		odm_set_bb_reg(dm, R_0x804, BIT(31), 0);
		odm_set_bb_reg(dm, R_0xc00, BIT(25) | BIT(24), 0);
		mapping_result =  true;
	} else if (channel == 155 && bw == CHANNEL_WIDTH_80) {
		odm_set_bb_reg(dm, R_0x804, BIT(31), 0);
		odm_set_bb_reg(dm, R_0xc00, BIT(25) | BIT(24), 0);
		mapping_result =  true;
	} else {
		odm_set_bb_reg(dm, R_0x804, BIT(31), 1);
		odm_set_bb_reg(dm, R_0xc00, BIT(25) | BIT(24), 1);
	}
#endif
	return mapping_result;
}

enum odm_rf_band phydm_ch_to_rf_band(void *dm_void, u8 central_ch)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	enum odm_rf_band rf_band = ODM_RF_BAND_5G_LOW;

	if (central_ch <= 14)
		rf_band = ODM_RF_BAND_2G;
	else if (central_ch >= 36 && central_ch <= 64)
		rf_band = ODM_RF_BAND_5G_LOW;
	else if ((central_ch >= 100) && (central_ch <= 144))
		rf_band = ODM_RF_BAND_5G_MID;
	else if (central_ch >= 149)
		rf_band = ODM_RF_BAND_5G_HIGH;
	else
		PHYDM_DBG(dm, ODM_COMP_API, "mapping channel to band fail\n");

	return rf_band;
}

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
u32 phydm_rf_psd_jgr3(void *dm_void, u8 path, u32 tone_idx)
{
#if (RTL8198F_SUPPORT || RTL8814B_SUPPORT)
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 reg_1b04 = 0, reg_1b08 = 0, reg_1b0c_11_10 = 0;
	u32 reg_1b14 = 0, reg_1b18 = 0, reg_1b1c = 0;
	u32 reg_1b28 = 0;
	u32 reg_1bcc_5_0 = 0;
	u32 reg_1b2c_27_16 = 0, reg_1b34 = 0, reg_1bd4 = 0;
	u32 reg_180c = 0, reg_410c = 0, reg_520c = 0, reg_530c = 0;
	u32 igi = 0;
	u32 i = 0;
	u32 psd_val = 0, psd_val_msb = 0, psd_val_lsb = 0, psd_max = 0;
	u32 psd_status_temp = 0;
	u16 poll_cnt = 0;

	/*read and record the ori. value*/
	reg_1b04 = odm_get_bb_reg(dm, R_0x1b04, MASKDWORD);
	reg_1b08 = odm_get_bb_reg(dm, R_0x1b08, MASKDWORD);
	reg_1b0c_11_10 = odm_get_bb_reg(dm, R_0x1b0c, 0xc00);
	reg_1b14 = odm_get_bb_reg(dm, R_0x1b14, MASKDWORD);
	reg_1b18 = odm_get_bb_reg(dm, R_0x1b18, MASKDWORD);
	reg_1b1c = odm_get_bb_reg(dm, R_0x1b1c, MASKDWORD);
	reg_1b28 = odm_get_bb_reg(dm, R_0x1b28, MASKDWORD);
	reg_1bcc_5_0 = odm_get_bb_reg(dm, R_0x1bcc, 0x3f);
	reg_1b2c_27_16 = odm_get_bb_reg(dm, R_0x1b2c, 0xfff0000);
	reg_1b34 = odm_get_bb_reg(dm, R_0x1b34, MASKDWORD);
	reg_1bd4 = odm_get_bb_reg(dm, R_0x1bd4, MASKDWORD);
	igi = odm_get_bb_reg(dm, R_0x1d70, MASKDWORD);
	reg_180c = odm_get_bb_reg(dm, R_0x180c, 0x3);
	reg_410c = odm_get_bb_reg(dm, R_0x410c, 0x3);
	reg_520c = odm_get_bb_reg(dm, R_0x520c, 0x3);
	reg_530c = odm_get_bb_reg(dm, R_0x530c, 0x3);

	/*rf psd reg setting*/
	odm_set_bb_reg(dm, R_0x1b00, 0x6, path); /*path is RF_path*/
	odm_set_bb_reg(dm, R_0x1b04, MASKDWORD, 0x0);
	odm_set_bb_reg(dm, R_0x1b08, MASKDWORD, 0x80);
	odm_set_bb_reg(dm, R_0x1b0c, 0xc00, 0x3);
	odm_set_bb_reg(dm, R_0x1b14, MASKDWORD, 0x0);
	odm_set_bb_reg(dm, R_0x1b18, MASKDWORD, 0x1);
/*#if (DM_ODM_SUPPORT_TYPE == ODM_AP)*/
	odm_set_bb_reg(dm, R_0x1b1c, MASKDWORD, 0x82103D21);
/*#else*/
	/*odm_set_bb_reg(dm, R_0x1b1c, MASKDWORD, 0x821A3D21);*/
/*#endif*/
	odm_set_bb_reg(dm, R_0x1b28, MASKDWORD, 0x0);
	odm_set_bb_reg(dm, R_0x1bcc, 0x3f, 0x3f);
	odm_set_bb_reg(dm, R_0x8a0, 0xf, 0x0); /* AGC off */
	odm_set_bb_reg(dm, R_0x1d70, MASKDWORD, 0x20202020);

	for (i = tone_idx - 1; i <= tone_idx + 1; i++) {
		/*set psd tone_idx for detection*/
		odm_set_bb_reg(dm, R_0x1b2c, 0xfff0000, i);
		/*one shot for RXIQK psd*/
		odm_set_bb_reg(dm, R_0x1b34, MASKDWORD, 0x1);
		odm_set_bb_reg(dm, R_0x1b34, MASKDWORD, 0x0);

		if (dm->support_ic_type & ODM_RTL8814B)
			for (poll_cnt = 0; poll_cnt < 20; poll_cnt++) {
				odm_set_bb_reg(dm, R_0x1bd4, 0x3f0000, 0x2b);
				psd_status_temp = odm_get_bb_reg(dm, R_0x1bfc,
								 BIT(1));
				if (!psd_status_temp)
					ODM_delay_us(10);
				else
					break;
			}
		else
			ODM_delay_us(250);

		/*read RxIQK power*/
		odm_set_bb_reg(dm, R_0x1bd4, MASKDWORD, 0x00250001);
		if (dm->support_ic_type & ODM_RTL8814B)
			psd_val_msb = odm_get_bb_reg(dm, R_0x1bfc, 0x7ff0000);
		else if (dm->support_ic_type & ODM_RTL8198F)
			psd_val_msb = odm_get_bb_reg(dm, R_0x1bfc, 0x1f0000);

		odm_set_bb_reg(dm, R_0x1bd4, MASKDWORD, 0x002e0001);
		psd_val_lsb = odm_get_bb_reg(dm, R_0x1bfc, MASKDWORD);
		if (dm->support_ic_type & ODM_RTL8814B)
			psd_val = (psd_val_msb << 21) + (psd_val_lsb >> 11);
		else if (dm->support_ic_type & ODM_RTL8198F)
			psd_val = (psd_val_msb << 27) + (psd_val_lsb >> 5);

		if (psd_val > psd_max)
			psd_max = psd_val;
	}

	/*refill the ori. value*/
	odm_set_bb_reg(dm, R_0x1b00, 0x6, path);
	odm_set_bb_reg(dm, R_0x1b04, MASKDWORD, reg_1b04);
	odm_set_bb_reg(dm, R_0x1b08, MASKDWORD, reg_1b08);
	odm_set_bb_reg(dm, R_0x1b0c, 0xc00, reg_1b0c_11_10);
	odm_set_bb_reg(dm, R_0x1b14, MASKDWORD, reg_1b14);
	odm_set_bb_reg(dm, R_0x1b18, MASKDWORD, reg_1b18);
	odm_set_bb_reg(dm, R_0x1b1c, MASKDWORD, reg_1b1c);
	odm_set_bb_reg(dm, R_0x1b28, MASKDWORD, reg_1b28);
	odm_set_bb_reg(dm, R_0x1bcc, 0x3f, reg_1bcc_5_0);
	odm_set_bb_reg(dm, R_0x1b2c, 0xfff0000, reg_1b2c_27_16);
	odm_set_bb_reg(dm, R_0x1b34, MASKDWORD, reg_1b34);
	odm_set_bb_reg(dm, R_0x1bd4, MASKDWORD, reg_1bd4);
	odm_set_bb_reg(dm, R_0x8a0, 0xf, 0xf); /* AGC on */
	odm_set_bb_reg(dm, R_0x1d70, MASKDWORD, igi);
	PHYDM_DBG(dm, ODM_COMP_API, "psd_max %d\n", psd_max);

	return psd_max;
#else
	return 0;
#endif
}

u8 phydm_find_intf_distance_jgr3(void *dm_void, u32 bw, u32 fc,
				 u32 f_interference, u32 *tone_idx_tmp_in)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 bw_up = 0, bw_low = 0;
	u32 int_distance = 0;
	u32 tone_idx_tmp = 0;
	u8 set_result = PHYDM_SET_NO_NEED;
	u8 channel = *dm->channel;

	bw_up = 1000 * (fc + bw / 2);
	bw_low = 1000 * (fc - bw / 2);
	fc = 1000 * fc;

	PHYDM_DBG(dm, ODM_COMP_API,
		  "[f_l, fc, fh] = [ %d, %d, %d ], f_int = ((%d))\n", bw_low,
		  fc, bw_up, f_interference);

	if (f_interference >= bw_low && f_interference <= bw_up) {
		int_distance = DIFF_2(fc, f_interference);
		/*@10*(int_distance /0.3125)*/
		if (channel < 15 &&
		    (dm->support_ic_type & (ODM_RTL8814B | ODM_RTL8198F)))
			tone_idx_tmp = int_distance / 312;
		else
			tone_idx_tmp = ((int_distance + 156) / 312);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "int_distance = ((%d)) , tone_idx_tmp = ((%d))\n",
			  int_distance, tone_idx_tmp);
		*tone_idx_tmp_in = tone_idx_tmp;
		set_result = PHYDM_SET_SUCCESS;
	}

	return set_result;
}

u8 phydm_csi_mask_setting_jgr3(void *dm_void, u32 enable, u32 ch, u32 bw,
			       u32 f_intf, u32 sec_ch, u8 wgt)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 fc = 2412;
	u8 direction = FREQ_POSITIVE;
	u32 tone_idx = 0;
	u8 set_result = PHYDM_SET_SUCCESS;
	u8 rpt = 0;

	if (enable == FUNC_DISABLE) {
		phydm_csi_mask_enable(dm, FUNC_ENABLE);
		phydm_clean_all_csi_mask(dm);
		phydm_csi_mask_enable(dm, FUNC_DISABLE);
		set_result = PHYDM_SET_SUCCESS;
	} else {
		PHYDM_DBG(dm, ODM_COMP_API,
			  "[Set CSI MASK] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s)), wgt = ((%d))\n",
			  ch, bw, f_intf,
			  (((bw == 20) || (ch > 14)) ? "Don't care" :
			  (sec_ch == PHYDM_ABOVE) ? "H" : "L"), wgt);

		/*@calculate fc*/
		if (phydm_find_fc(dm, ch, bw, sec_ch, &fc) == PHYDM_SET_FAIL) {
			set_result = PHYDM_SET_FAIL;
		} else {
			/*@calculate interference distance*/
			rpt = phydm_find_intf_distance_jgr3(dm, bw, fc, f_intf,
							    &tone_idx);
			if (rpt == PHYDM_SET_SUCCESS) {
				if (f_intf >= 1000 * fc)
					direction = FREQ_POSITIVE;
				else
					direction = FREQ_NEGATIVE;

				phydm_csi_mask_enable(dm, FUNC_ENABLE);
				phydm_set_csi_mask_jgr3(dm, tone_idx, direction,
							wgt);
				set_result = PHYDM_SET_SUCCESS;
			} else {
				set_result = PHYDM_SET_NO_NEED;
			}
		}
		if (!(set_result == PHYDM_SET_SUCCESS))
			phydm_csi_mask_enable(dm, FUNC_DISABLE);
	}

	return set_result;
}

void phydm_set_csi_mask_jgr3(void *dm_void, u32 tone_idx_tmp, u8 tone_direction,
			     u8 wgt)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 multi_tone_idx_tmp = 0;
	u32 reg_tmp = 0;
	u32 tone_num = 64;
	u32 table_addr = 0;
	u32 addr = 0;
	u8 rf_bw = 0;
	u8 value = 0;
	u8 channel = *dm->channel;

	rf_bw = odm_read_1byte(dm, R_0x9b0);
	if (((rf_bw & 0xc) >> 2) == 0x2)
		tone_num = 128; /* @RF80 : tone(-1) at tone_idx=255 */
	else
		tone_num = 64; /* @RF20/40 : tone(-1) at tone_idx=127 */

	if (tone_direction == FREQ_POSITIVE) {
		if (tone_idx_tmp >= (tone_num - 1))
			tone_idx_tmp = (tone_num - 1);
	} else {
		if (tone_idx_tmp >= tone_num)
			tone_idx_tmp = tone_num;

		tone_idx_tmp = (tone_num << 1) - tone_idx_tmp;
	}
	table_addr = tone_idx_tmp >> 1;

	reg_tmp = odm_read_4byte(dm, R_0x1d94);
	PHYDM_DBG(dm, ODM_COMP_API,
		  "Pre Mask tone idx[%d]: Reg0x1d94 = ((0x%x))\n",
		  tone_idx_tmp, reg_tmp);
	odm_set_bb_reg(dm, R_0x1ee8, 0x3, 0x3);
	odm_set_bb_reg(dm, R_0x1d94, BIT(31) | BIT(30), 0x1);

	if (channel < 15 &&
	    (dm->support_ic_type & (ODM_RTL8814B | ODM_RTL8198F))) {
		if (tone_idx_tmp % 2 == 1) {
			if (tone_direction == FREQ_POSITIVE) {
				/*===Tone 1===*/
				odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2,
					       (table_addr & 0xff));
				value = (BIT(3) | (wgt & 0x7)) << 4;
				odm_set_bb_reg(dm, R_0x1d94, 0xff, value);
				reg_tmp = odm_read_4byte(dm, R_0x1d94);
				PHYDM_DBG(dm, ODM_COMP_API,
					  "New Mask tone 1 idx[%d]: Reg0x1d94 = ((0x%x))\n",
					  tone_idx_tmp, reg_tmp);
				/*===Tone 2===*/
				value = 0;
				multi_tone_idx_tmp = tone_idx_tmp + 1;
				table_addr = multi_tone_idx_tmp >> 1;
				odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2,
					       (table_addr & 0xff));
				value = (BIT(3) | (wgt & 0x7));
				odm_set_bb_reg(dm, R_0x1d94, 0xff, value);
				reg_tmp = odm_read_4byte(dm, R_0x1d94);
				PHYDM_DBG(dm, ODM_COMP_API,
					  "New Mask tone 2 idx[%d]: Reg0x1d94 = ((0x%x))\n",
					  tone_idx_tmp, reg_tmp);
			} else {
				/*===Tone 1 & 2===*/
				odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2,
					       (table_addr & 0xff));
				value = ((BIT(3) | (wgt & 0x7)) << 4) |
					(BIT(3) | (wgt & 0x7));
				odm_set_bb_reg(dm, R_0x1d94, 0xff, value);
				reg_tmp = odm_read_4byte(dm, R_0x1d94);
				PHYDM_DBG(dm, ODM_COMP_API,
					  "New Mask tone 1 & 2 idx[%d]: Reg0x1d94 = ((0x%x))\n",
					  tone_idx_tmp, reg_tmp);
			}
		} else {
			if (tone_direction == FREQ_POSITIVE) {
				/*===Tone 1 & 2===*/
				odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2,
					       (table_addr & 0xff));
				value = ((BIT(3) | (wgt & 0x7)) << 4) |
					(BIT(3) | (wgt & 0x7));
				odm_set_bb_reg(dm, R_0x1d94, 0xff, value);
				reg_tmp = odm_read_4byte(dm, R_0x1d94);
				PHYDM_DBG(dm, ODM_COMP_API,
					  "New Mask tone 1 & 2 idx[%d]: Reg0x1d94 = ((0x%x))\n",
					  tone_idx_tmp, reg_tmp);
			} else {
				/*===Tone 1===*/
				odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2,
					       (table_addr & 0xff));
				value = (BIT(3) | (wgt & 0x7));
				odm_set_bb_reg(dm, R_0x1d94, 0xff, value);
				reg_tmp = odm_read_4byte(dm, R_0x1d94);
				PHYDM_DBG(dm, ODM_COMP_API,
					  "New Mask tone 1 idx[%d]: Reg0x1d94 = ((0x%x))\n",
					  tone_idx_tmp, reg_tmp);

				/*===Tone 2===*/
				value = 0;
				multi_tone_idx_tmp = tone_idx_tmp - 1;
				table_addr = multi_tone_idx_tmp >> 1;
				odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2,
					       (table_addr & 0xff));
				value = (BIT(3) | (wgt & 0x7)) << 4;
				odm_set_bb_reg(dm, R_0x1d94, 0xff, value);
				reg_tmp = odm_read_4byte(dm, R_0x1d94);
				PHYDM_DBG(dm, ODM_COMP_API,
					  "New Mask tone 2 idx[%d]: Reg0x1d94 = ((0x%x))\n",
					  tone_idx_tmp, reg_tmp);
			}
		}
	} else {
		if ((dm->support_ic_type & (ODM_RTL8814B)) &&
		    phydm_spur_case_mapping(dm)) {
			if (!(tone_idx_tmp % 2)) {
				odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2,
					       (table_addr & 0xff));
				value = ((BIT(3) | (((wgt + 4) <= 7 ? (wgt +
					 4) : 7) & 0x7)) << 4) | (BIT(3) |
					 (wgt & 0x7));
				odm_set_bb_reg(dm, R_0x1d94, 0xff, value);
				reg_tmp = odm_read_4byte(dm, R_0x1d94);
				PHYDM_DBG(dm, ODM_COMP_API,
					  "New Mask tone idx[%d]: Reg0x1d94 = ((0x%x))\n",
					  tone_idx_tmp, reg_tmp);
				if (tone_idx_tmp == 0)
					table_addr = tone_num - 1;
				else
					table_addr = table_addr - 1;
				if (tone_idx_tmp != tone_num) {
					odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2,
						       (table_addr & 0xff));
					value = (BIT(3) | (((wgt + 4) <= 7 ?
						 (wgt + 4) : 7) & 0x7)) << 4;
					odm_set_bb_reg(dm, R_0x1d94, 0xff,
						       value);
					reg_tmp = odm_read_4byte(dm, R_0x1d94);
					PHYDM_DBG(dm, ODM_COMP_API,
						  "New Mask Reg0x1d94 = ((0x%x))\n",
						  reg_tmp);
				}
			} else {
				odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2,
					       (table_addr & 0xff));
				value = ((BIT(3) | (wgt & 0x7)) << 4) |
					 (BIT(3) | (((wgt + 4) <= 7 ? (wgt +
					  4) : 7) & 0x7));
				odm_set_bb_reg(dm, R_0x1d94, 0xff, value);
				reg_tmp = odm_read_4byte(dm, R_0x1d94);
				PHYDM_DBG(dm, ODM_COMP_API,
					  "New Mask tone idx[%d]: Reg0x1d94 = ((0x%x))\n",
					  tone_idx_tmp, reg_tmp);
				if (tone_idx_tmp == (tone_num << 1) - 1)
					table_addr = 0;
				else
					table_addr = table_addr + 1;
				if (tone_idx_tmp != tone_num - 1) {
					odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2,
						       (table_addr & 0xff));
					value = (BIT(3) | (((wgt + 4) <= 7 ?
						 (wgt + 4) : 7) & 0x7));
					odm_set_bb_reg(dm, R_0x1d94, 0xff,
						       value);
					reg_tmp = odm_read_4byte(dm, R_0x1d94);
					PHYDM_DBG(dm, ODM_COMP_API,
						  "New Mask Reg0x1d94 = ((0x%x))\n",
						  reg_tmp);
				}
			}
		} else {
			odm_set_bb_reg(dm, R_0x1d94, MASKBYTE2, (table_addr &
				       0xff));
			if (tone_idx_tmp % 2)
				value = (BIT(3) | (wgt & 0x7)) << 4;
			else
				value = BIT(3) | (wgt & 0x7);

			odm_set_bb_reg(dm, R_0x1d94, 0xff, value);
			reg_tmp = odm_read_4byte(dm, R_0x1d94);
			PHYDM_DBG(dm, ODM_COMP_API,
				  "New Mask tone idx[%d]: Reg0x1d94 = ((0x%x))\n",
				  tone_idx_tmp, reg_tmp);
		}
	}
	odm_set_bb_reg(dm, R_0x1ee8, 0x3, 0x0);
}

void phydm_nbi_reset_jgr3(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	odm_set_bb_reg(dm, R_0x818, BIT(3), 1);
	odm_set_bb_reg(dm, R_0x1d3c, 0x78000000, 0);
	odm_set_bb_reg(dm, R_0x818, BIT(3), 0);
	odm_set_bb_reg(dm, R_0x818, BIT(11), 0);
	#if RTL8814B_SUPPORT
	if (dm->support_ic_type & ODM_RTL8814B) {
		odm_set_bb_reg(dm, R_0x1944, 0x300, 0x3);
		odm_set_bb_reg(dm, R_0x4044, 0x300, 0x3);
		odm_set_bb_reg(dm, R_0x5044, 0x300, 0x3);
		odm_set_bb_reg(dm, R_0x5144, 0x300, 0x3);
		odm_set_bb_reg(dm, R_0x810, 0xf, 0x0);
		odm_set_bb_reg(dm, R_0x810, 0xf0000, 0x0);
		odm_set_bb_reg(dm, R_0xc24, MASKDWORD, 0x406000ff);
	}
	#endif
}

u8 phydm_nbi_setting_jgr3(void *dm_void, u32 enable, u32 ch, u32 bw, u32 f_intf,
			  u32 sec_ch, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 fc = 2412;
	u8 direction = FREQ_POSITIVE;
	u32 tone_idx = 0;
	u8 set_result = PHYDM_SET_SUCCESS;
	u8 rpt = 0;

	if (enable == FUNC_DISABLE) {
		phydm_nbi_reset_jgr3(dm);
		set_result = PHYDM_SET_SUCCESS;
	} else {
		PHYDM_DBG(dm, ODM_COMP_API,
			  "[Set NBI] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
			  ch, bw, f_intf,
			  (((sec_ch == PHYDM_DONT_CARE) || (bw == 20) ||
			  (ch > 14)) ? "Don't care" :
			  (sec_ch == PHYDM_ABOVE) ? "H" : "L"));

		/*@calculate fc*/
		if (phydm_find_fc(dm, ch, bw, sec_ch, &fc) == PHYDM_SET_FAIL) {
			set_result = PHYDM_SET_FAIL;
		} else {
			/*@calculate interference distance*/
			rpt = phydm_find_intf_distance_jgr3(dm, bw, fc, f_intf,
							    &tone_idx);
			if (rpt == PHYDM_SET_SUCCESS) {
				if (f_intf >= 1000 * fc)
					direction = FREQ_POSITIVE;
				else
					direction = FREQ_NEGATIVE;

				phydm_set_nbi_reg_jgr3(dm, tone_idx, direction,
						       path);
				set_result = PHYDM_SET_SUCCESS;
			} else {
				set_result = PHYDM_SET_NO_NEED;
			}
		}
	}

	if (set_result == PHYDM_SET_SUCCESS)
		phydm_nbi_enable_jgr3(dm, enable, path);
	else
		phydm_nbi_enable_jgr3(dm, FUNC_DISABLE, path);

	if (dm->support_ic_type & ODM_RTL8814B)
		odm_set_bb_reg(dm, R_0x1d3c, BIT(19), 0);

	return set_result;
}

void phydm_set_nbi_reg_jgr3(void *dm_void, u32 tone_idx_tmp, u8 tone_direction,
			    u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 reg_tmp_value = 0;
	u32 tone_num = 64;
	u32 addr = 0;
	u8 rf_bw = 0;

	rf_bw = odm_read_1byte(dm, R_0x9b0);
	if (((rf_bw & 0xc) >> 2) == 0x2)
		tone_num = 128; /* RF80 : tone-1 at tone_idx=255 */
	else
		tone_num = 64; /* RF20/40 : tone-1 at tone_idx=127 */

	if (tone_direction == FREQ_POSITIVE) {
		if (tone_idx_tmp >= (tone_num - 1))
			tone_idx_tmp = (tone_num - 1);
	} else {
		if (tone_idx_tmp >= tone_num)
			tone_idx_tmp = tone_num;

		tone_idx_tmp = (tone_num << 1) - tone_idx_tmp;
	}
	/*Mark the tone idx for Packet detection*/
	#if RTL8814B_SUPPORT
	if (dm->support_ic_type & ODM_RTL8814B) {
		odm_set_bb_reg(dm, R_0xc24, 0xff, 0xff);
		if ((*dm->channel == 5) &&
		    (*dm->band_width == CHANNEL_WIDTH_40))
			odm_set_bb_reg(dm, R_0xc24, 0xff00, 0x1a);
		else
			odm_set_bb_reg(dm, R_0xc24, 0xff00, tone_idx_tmp);
	}
	#endif
	switch (path) {
	case RF_PATH_A:
		odm_set_bb_reg(dm, R_0x1944, 0x001FF000, tone_idx_tmp);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "Set tone idx[%d]:PATH-A = ((0x%x))\n",
			  tone_idx_tmp, tone_idx_tmp);
		break;
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	case RF_PATH_B:
		odm_set_bb_reg(dm, R_0x4044, 0x001FF000, tone_idx_tmp);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "Set tone idx[%d]:PATH-B = ((0x%x))\n",
			  tone_idx_tmp, tone_idx_tmp);
		break;
	#endif
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	case RF_PATH_C:
		odm_set_bb_reg(dm, R_0x5044, 0x001FF000, tone_idx_tmp);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "Set tone idx[%d]:PATH-C = ((0x%x))\n",
			  tone_idx_tmp, tone_idx_tmp);
		break;
	#endif
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	case RF_PATH_D:
		odm_set_bb_reg(dm, R_0x5144, 0x001FF000, tone_idx_tmp);
		PHYDM_DBG(dm, ODM_COMP_API,
			  "Set tone idx[%d]:PATH-D = ((0x%x))\n",
			  tone_idx_tmp, tone_idx_tmp);
		break;
	#endif
	default:
		break;
	}
}

void phydm_nbi_enable_jgr3(void *dm_void, u32 enable, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean val = false;

	val = (enable == FUNC_ENABLE) ? true : false;

	PHYDM_DBG(dm, ODM_COMP_API, "Enable NBI=%d\n", val);

	if (dm->support_ic_type & ODM_RTL8814B) {
		odm_set_bb_reg(dm, R_0x1d3c, BIT(19), val);
		odm_set_bb_reg(dm, R_0x818, BIT(3), val);
	} else if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8812F)) {
		odm_set_bb_reg(dm, R_0x818, BIT(3), !val);
	}
	odm_set_bb_reg(dm, R_0x818, BIT(11), val);
	odm_set_bb_reg(dm, R_0x1d3c, 0x78000000, 0xf);

	if (enable == FUNC_ENABLE) {
		switch (path) {
		case RF_PATH_A:
			odm_set_bb_reg(dm, R_0x1940, BIT(31), val);
			break;
		#if (defined(PHYDM_COMPILE_ABOVE_2SS))
		case RF_PATH_B:
			odm_set_bb_reg(dm, R_0x4040, BIT(31), val);
			break;
		#endif
		#if (defined(PHYDM_COMPILE_ABOVE_3SS))
		case RF_PATH_C:
			odm_set_bb_reg(dm, R_0x5040, BIT(31), val);
			break;
		#endif
		#if (defined(PHYDM_COMPILE_ABOVE_4SS))
		case RF_PATH_D:
			odm_set_bb_reg(dm, R_0x5140, BIT(31), val);
			break;
		#endif
		default:
			break;
		}
	} else {
		odm_set_bb_reg(dm, R_0x1940, BIT(31), val);
		#if (defined(PHYDM_COMPILE_ABOVE_2SS))
		odm_set_bb_reg(dm, R_0x4040, BIT(31), val);
		#endif
		#if (defined(PHYDM_COMPILE_ABOVE_3SS))
		odm_set_bb_reg(dm, R_0x5040, BIT(31), val);
		#endif
		#if (defined(PHYDM_COMPILE_ABOVE_4SS))
		odm_set_bb_reg(dm, R_0x5140, BIT(31), val);
		#endif
		#if RTL8812F_SUPPORT
		if (dm->support_ic_type & ODM_RTL8812F) {
			odm_set_bb_reg(dm, R_0x818, BIT(3), val);
			odm_set_bb_reg(dm, R_0x1d3c, 0x78000000, 0x0);
		}
		#endif
	}
}

u8 phydm_phystat_rpt_jgr3(void *dm_void, enum phystat_rpt info,
			  enum rf_path ant_path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	s8 evm_org, cfo_org, rxsnr_org;
	u8 i, return_info = 0, tmp_lsb = 0, tmp_msb = 0, tmp_info = 0;

	/* Update the status for each pkt */
	odm_set_bb_reg(dm, R_0x8c4, 0xfff000, 0x448);
	odm_set_bb_reg(dm, R_0x8c0, MASKLWORD, 0x4001);
	/* PHY status Page1 */
	odm_set_bb_reg(dm, R_0x8c0, 0x3C00000, 0x1);
	/*choose debug port for phystatus */
	odm_set_bb_reg(dm, R_0x1c3c, 0xFFF00, 0x380);

	if (info == PHY_PWDB) {
		/* Choose the report of the diff path */
		if (ant_path == RF_PATH_A)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x1);
		else if (ant_path == RF_PATH_B)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x2);
		else if (ant_path == RF_PATH_C)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x3);
		else if (ant_path == RF_PATH_D)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x4);
	} else if (info == PHY_EVM) {
		/* Choose the report of the diff path */
		if (ant_path == RF_PATH_A)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x10);
		else if (ant_path == RF_PATH_B)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x11);
		else if (ant_path == RF_PATH_C)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x12);
		else if (ant_path == RF_PATH_D)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x13);
		return_info = (u8)odm_get_bb_reg(dm, R_0x2dbc, 0xff);
	} else if (info == PHY_CFO) {
		/* Choose the report of the diff path */
		if (ant_path == RF_PATH_A)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x14);
		else if (ant_path == RF_PATH_B)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x15);
		else if (ant_path == RF_PATH_C)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x16);
		else if (ant_path == RF_PATH_D)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x17);
		return_info = (u8)odm_get_bb_reg(dm, R_0x2dbc, 0xff);
	} else if (info == PHY_RXSNR) {
		/* Choose the report of the diff path */
		if (ant_path == RF_PATH_A)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x18);
		else if (ant_path == RF_PATH_B)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x19);
		else if (ant_path == RF_PATH_C)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x1a);
		else if (ant_path == RF_PATH_D)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x1b);
		return_info = (u8)odm_get_bb_reg(dm, R_0x2dbc, 0xff);
	} else if (info == PHY_LGAIN) {
		/* choose page */
		odm_set_bb_reg(dm, R_0x8c0, 0x3c00000, 0x2);
		/* Choose the report of the diff path */
		if (ant_path == RF_PATH_A) {
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0xd);
			tmp_info = (u8)odm_get_bb_reg(dm, R_0x2dbc, 0x3f);
			return_info = tmp_info;
		} else if (ant_path == RF_PATH_B) {
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0xd);
			tmp_lsb = (u8)odm_get_bb_reg(dm, R_0x2dbc, 0xc0);
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0xe);
			tmp_msb = (u8)odm_get_bb_reg(dm, R_0x2dbc, 0xf);
			tmp_info |= (tmp_msb << 2) | tmp_lsb;
			return_info = tmp_info;
		} else if (ant_path == RF_PATH_C) {
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0xe);
			tmp_lsb = (u8)odm_get_bb_reg(dm, R_0x2dbc, 0xf0);
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0xf);
			tmp_msb = (u8)odm_get_bb_reg(dm, R_0x2dbc, 0x3);
			tmp_info |= (tmp_msb << 4) | tmp_lsb;
			return_info = tmp_info;
		} else if (ant_path == RF_PATH_D) {
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x10);
			tmp_info = (u8)odm_get_bb_reg(dm, R_0x2dbc, 0x3f);
			return_info = tmp_info;
		}
	} else if (info == PHY_HT_AAGC_GAIN) {
		/* choose page */
		odm_set_bb_reg(dm, R_0x8c0, 0x3c00000, 0x2);
		/* Choose the report of the diff path */
		if (ant_path == RF_PATH_A)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x12);
		else if (ant_path == RF_PATH_B)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x13);
		else if (ant_path == RF_PATH_C)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x14);
		else if (ant_path == RF_PATH_D)
			odm_set_bb_reg(dm, R_0x8c4, 0x3ff, 0x15);
		return_info = (u8)odm_get_bb_reg(dm, R_0x2dbc, 0xff);
	}
	return return_info;
}

void phydm_ex_hal8814b_wifi_only_hw_config(void *dm_void)
{
	/*BB control*/
	/*halwifionly_phy_set_bb_reg(pwifionlycfg, 0x4c, 0x01800000, 0x2);*/
	/*SW control*/
	/*halwifionly_phy_set_bb_reg(pwifionlycfg, 0xcb4, 0xff, 0x77);*/
	/*antenna mux switch */
	/*halwifionly_phy_set_bb_reg(pwifionlycfg, 0x974, 0x300, 0x3);*/

	/*halwifionly_phy_set_bb_reg(pwifionlycfg, 0x1990, 0x300, 0x0);*/

	/*halwifionly_phy_set_bb_reg(pwifionlycfg, 0xcbc, 0x80000, 0x0);*/
	/*switch to WL side controller and gnt_wl gnt_bt debug signal */
	/*halwifionly_phy_set_bb_reg(pwifionlycfg, 0x70, 0xff000000, 0x0e);*/
	/*gnt_wl=1 , gnt_bt=0*/
	/*halwifionly_phy_set_bb_reg(pwifionlycfg, 0x1704, 0xffffffff,
	 *			     0x7700);
	 */
	/*halwifionly_phy_set_bb_reg(pwifionlycfg, 0x1700, 0xffffffff,
	 *			     0xc00f0038);
	 */
}

void phydm_user_position_for_sniffer(void *dm_void, u8 user_position)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	/* user position valid */
	odm_set_bb_reg(dm, R_0xa68, BIT(17), 1);
	/* Select user seat from pmac */
	odm_set_bb_reg(dm, R_0xa68, BIT(16), 1);
	/*user seat*/
	odm_set_bb_reg(dm, R_0xa68, (BIT(19) | BIT(18)), user_position);
}

boolean
phydm_bb_ctrl_txagc_ofst_jgr3(void *dm_void, s8 pw_offset, /*@(unit: dB)*/
			      u8 add_half_db /*@(+0.5 dB)*/)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	s8 pw_idx = pw_offset * 4; /*@ 7Bit, 0.25dB unit*/

	if (pw_offset < -16 || pw_offset > 15) {
		pr_debug("[Warning][%s]Ofst error=%d", __func__, pw_offset);
		return false;
	}

	if (add_half_db)
		pw_idx += 2;

	PHYDM_DBG(dm, ODM_COMP_API, "Pw_ofst=0x%x\n", pw_idx);

	odm_set_bb_reg(dm, R_0x18a0, 0x3f, pw_idx);

	if (dm->num_rf_path >= 2)
		odm_set_bb_reg(dm, R_0x41a0, 0x3f, pw_idx);

	if (dm->num_rf_path >= 3)
		odm_set_bb_reg(dm, R_0x52a0, 0x3f, pw_idx);

	if (dm->num_rf_path >= 4)
		odm_set_bb_reg(dm, R_0x53a0, 0x3f, pw_idx);

	return true;
}

#endif
u8 phydm_nbi_setting(void *dm_void, u32 enable, u32 ch, u32 bw, u32 f_intf,
		     u32 sec_ch)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 fc = 2412;
	u8 direction = FREQ_POSITIVE;
	u32 tone_idx = 0;
	u8 set_result = PHYDM_SET_SUCCESS;
	u8 rpt = 0;

	if (enable == FUNC_DISABLE) {
		set_result = PHYDM_SET_SUCCESS;
	} else {
		PHYDM_DBG(dm, ODM_COMP_API,
			  "[Set NBI] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
			  ch, bw, f_intf,
			  (((sec_ch == PHYDM_DONT_CARE) || (bw == 20) ||
			  (ch > 14)) ? "Don't care" :
			  (sec_ch == PHYDM_ABOVE) ? "H" : "L"));

		/*@calculate fc*/
		if (phydm_find_fc(dm, ch, bw, sec_ch, &fc) == PHYDM_SET_FAIL) {
			set_result = PHYDM_SET_FAIL;
		} else {
			/*@calculate interference distance*/
			rpt = phydm_find_intf_distance(dm, bw, fc, f_intf,
						       &tone_idx);
			if (rpt == PHYDM_SET_SUCCESS) {
				if (f_intf >= fc)
					direction = FREQ_POSITIVE;
				else
					direction = FREQ_NEGATIVE;

				phydm_set_nbi_reg(dm, tone_idx, bw);

				set_result = PHYDM_SET_SUCCESS;
			} else {
				set_result = PHYDM_SET_NO_NEED;
		}
	}
	}

	if (set_result == PHYDM_SET_SUCCESS)
		phydm_nbi_enable(dm, enable);
	else
		phydm_nbi_enable(dm, FUNC_DISABLE);

	return set_result;
}

void phydm_nbi_debug(void *dm_void, char input[][16], u32 *_used, char *output,
		     u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 val[10] = {0};
	char help[] = "-h";
	u8 i = 0, input_idx = 0, idx_lmt = 0;
	u32 enable = 0; /*@function enable*/
	u32 ch = 0;
	u32 bw = 0;
	u32 f_int = 0; /*@interference frequency*/
	u32 sec_ch = 0; /*secondary channel*/
	u8 rpt = 0;
	u8 path = 0;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		idx_lmt = 6;
	else
		idx_lmt = 5;
	for (i = 0; i < idx_lmt; i++) {
		PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &val[i]);
		input_idx++;
	}

	if (input_idx == 0)
		return;

	enable = val[0];
	ch = val[1];
	bw = val[2];
	f_int = val[3];
	sec_ch = val[4];
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	path = (u8)val[5];
	#endif

	if ((strcmp(input[1], help) == 0)) {
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "{en:1 Dis:2} {ch} {BW:20/40/80} {f_intf(khz)} {Scnd_CH(L=1, H=2)} {Path:A~D(0~3)}\n");
		else
		#endif
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "{en:1 Dis:2} {ch} {BW:20/40/80} {f_intf(khz)} {Scnd_CH(L=1, H=2)}\n");
		*_used = used;
		*_out_len = out_len;
		return;
	} else if (val[0] == FUNC_ENABLE) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[Enable NBI] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
			 ch, bw, f_int,
			 ((sec_ch == PHYDM_DONT_CARE) ||
			 (bw == 20) || (ch > 14)) ? "Don't care" :
			 ((sec_ch == PHYDM_ABOVE) ? "H" : "L"));
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			rpt = phydm_nbi_setting_jgr3(dm, enable, ch, bw, f_int,
						     sec_ch, path);
		else
		#endif
			rpt = phydm_nbi_setting(dm, enable, ch, bw, f_int,
						sec_ch);
	} else if (val[0] == FUNC_DISABLE) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[Disable NBI]\n");
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			rpt = phydm_nbi_setting_jgr3(dm, enable, ch, bw, f_int,
						     sec_ch, path);
		else
		#endif
			rpt = phydm_nbi_setting(dm, enable, ch, bw, f_int,
						sec_ch);
	} else {
		rpt = PHYDM_SET_FAIL;
	}

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "[NBI set result: %s]\n",
		 (rpt == PHYDM_SET_SUCCESS) ? "Success" :
		 ((rpt == PHYDM_SET_NO_NEED) ? "No need" : "Error"));

	*_used = used;
	*_out_len = out_len;
}

void phydm_csi_debug(void *dm_void, char input[][16], u32 *_used, char *output,
		     u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 val[10] = {0};
	char help[] = "-h";
	u8 i = 0, input_idx = 0, idx_lmt = 0;
	u32 enable = 0;  /*@function enable*/
	u32 ch = 0;
	u32 bw = 0;
	u32 f_int = 0; /*@interference frequency*/
	u32 sec_ch = 0;  /*secondary channel*/
	u8 rpt = 0;
	u8 wgt = 0;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		idx_lmt = 6;
	else
		idx_lmt = 5;

	for (i = 0; i < idx_lmt; i++) {
		PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &val[i]);
		input_idx++;
	}

	if (input_idx == 0)
		return;

	enable = val[0];
	ch = val[1];
	bw = val[2];
	f_int = val[3];
	sec_ch = val[4];
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	wgt = (u8)val[5];
	#endif

	if ((strcmp(input[1], help) == 0)) {
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "{en:1 Dis:2} {ch} {BW:20/40/80} {f_intf(KHz)} {Scnd_CH(L=1, H=2)}\n{wgt:(7:3/4),(6~1: 1/2 ~ 1/64),(0:0)}\n");
		else
		#endif
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "{en:1 Dis:2} {ch} {BW:20/40/80} {f_intf(Mhz)} {Scnd_CH(L=1, H=2)}\n");

		*_used = used;
		*_out_len = out_len;
		return;

	} else if (val[0] == FUNC_ENABLE) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[Enable CSI MASK] CH = ((%d)), BW = ((%d)), f_intf = ((%d)), Scnd_CH = ((%s))\n",
			 ch, bw, f_int,
			 (ch > 14) ? "Don't care" :
			 (((sec_ch == PHYDM_DONT_CARE) ||
			 (bw == 20) || (ch > 14)) ? "H" : "L"));
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			rpt = phydm_csi_mask_setting_jgr3(dm, enable, ch, bw,
							  f_int, sec_ch, wgt);
		else
		#endif
			rpt = phydm_csi_mask_setting(dm, enable, ch, bw, f_int,
						     sec_ch);
	} else if (val[0] == FUNC_DISABLE) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[Disable CSI MASK]\n");
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			rpt = phydm_csi_mask_setting_jgr3(dm, enable, ch, bw,
							  f_int, sec_ch, wgt);
		else
		#endif
			rpt = phydm_csi_mask_setting(dm, enable, ch, bw, f_int,
						     sec_ch);
	} else {
		rpt = PHYDM_SET_FAIL;
	}
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "[CSI MASK set result: %s]\n",
		 (rpt == PHYDM_SET_SUCCESS) ? "Success" :
		 ((rpt == PHYDM_SET_NO_NEED) ? "No need" : "Error"));

	*_used = used;
	*_out_len = out_len;
}

void phydm_stop_ck320(void *dm_void, u8 enable)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 val = enable ? 1 : 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, R_0x8b4, BIT(6), val);
	} else {
		if (dm->support_ic_type & ODM_IC_N_2SS) /*N-2SS*/
			odm_set_bb_reg(dm, R_0x87c, BIT(29), val);
		else /*N-1SS*/
			odm_set_bb_reg(dm, R_0x87c, BIT(31), val);
	}
}

boolean
phydm_bb_ctrl_txagc_ofst(void *dm_void, s8 pw_offset, /*@(unit: dB)*/
			 u8 add_half_db /*@(+0.5 dB)*/)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	s8 pw_idx;
	u8 offset_bit_num = 0;

	if (dm->support_ic_type & N_IC_TX_OFFEST_5_BIT) {
		/*@ 5Bit, 0.5dB unit*/
		if (pw_offset < -8 || pw_offset > 7) {
			pr_debug("[Warning][%s] Ofst=%d", __func__, pw_offset);
			return false;
		}
		offset_bit_num = 5;
	} else {
		if (pw_offset < -16 || pw_offset > 15) {
			pr_debug("[Warning][%s] Ofst=%d", __func__, pw_offset);
			return false;
		}
		if (dm->support_ic_type & N_IC_TX_OFFEST_7_BIT) {
		/*@ 7Bit, 0.25dB unit*/
			offset_bit_num = 7;
		} else {
		/*@ 6Bit, 0.5dB unit*/
			offset_bit_num = 6;
		}
	}

	pw_idx = (offset_bit_num == 7) ? pw_offset * 4 : pw_offset * 2;

	if (add_half_db)
		pw_idx = (offset_bit_num == 7) ? pw_idx + 2 : pw_idx + 1;

	PHYDM_DBG(dm, ODM_COMP_API, "Pw_ofst=0x%x\n", pw_idx);

	switch (dm->ic_ip_series) {
	case PHYDM_IC_AC:
		odm_set_bb_reg(dm, R_0x8b4, 0x3f, pw_idx); /*6Bit*/
		break;
	case PHYDM_IC_N:
		if (offset_bit_num == 5) {
			odm_set_bb_reg(dm, R_0x80c, 0x1f00, pw_idx);
			if (dm->num_rf_path >= 2)
				odm_set_bb_reg(dm, R_0x80c, 0x3e000, pw_idx);
		} else if (offset_bit_num == 6) {
			odm_set_bb_reg(dm, R_0x80c, 0x3f00, pw_idx);
			if (dm->num_rf_path >= 2)
				odm_set_bb_reg(dm, R_0x80c, 0xfc000, pw_idx);
		}  else { /*7Bit*/
			odm_set_bb_reg(dm, R_0x80c, 0x7f00, pw_idx);
			if (dm->num_rf_path >= 2)
				odm_set_bb_reg(dm, R_0x80c, 0x3f8000, pw_idx);
		}
		break;
	}
	return true;
}

boolean
phydm_set_bb_txagc_offset(void *dm_void, s8 pw_offset, /*@(unit: dB)*/
			  u8 add_half_db /*@(+0.5 dB)*/)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean rpt = false;

	PHYDM_DBG(dm, ODM_COMP_API, "power_offset=%d, add_half_db =%d\n",
		  pw_offset, add_half_db);

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		rpt = phydm_bb_ctrl_txagc_ofst_jgr3(dm, pw_offset, add_half_db);
	} else
#endif
	{
		rpt = phydm_bb_ctrl_txagc_ofst(dm, pw_offset, add_half_db);
	}

	PHYDM_DBG(dm, ODM_COMP_API, "TX AGC Offset set_success=%d", rpt);

	return rpt;
}

#ifdef PHYDM_COMMON_API_SUPPORT
void phydm_reset_txagc(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 r_txagc_cck[4] = {R_0x18a0, R_0x41a0, R_0x52a0, R_0x53a0};
	u32 r_txagc_ofdm[4] = {R_0x18e8, R_0x41e8, R_0x52e8, R_0x53e8};
	u32 r_txagc_diff = R_0x3a00;
	u8 i = 0;

	if (!(dm->support_ic_type & ODM_IC_JGR3_SERIES)) {
		PHYDM_DBG(dm, ODM_COMP_API, "Only for JGR3 ICs!\n");
		return;
	}

	for (i = RF_PATH_A; i < dm->num_rf_path; i++) {
		odm_set_bb_reg(dm, r_txagc_cck[i], 0x7f0000, 0x0);
		odm_set_bb_reg(dm, r_txagc_ofdm[i], 0x1fc00, 0x0);
	}

	for (i = 0; i <= ODM_RATEVHTSS4MCS6; i = i + 4)
		odm_set_bb_reg(dm, r_txagc_diff + i, MASKDWORD, 0x0);
}
boolean
phydm_api_shift_txagc(void *dm_void, u32 pwr_offset, enum rf_path path,
		      boolean is_positive) {
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean ret = false;
	u32 txagc_cck = 0;
	u32 txagc_ofdm = 0;
	u32 r_txagc_ofdm[4] = {R_0x18e8, R_0x41e8, R_0x52e8, R_0x53e8};
	u32 r_txagc_cck[4] = {R_0x18a0, R_0x41a0, R_0x52a0, R_0x53a0};

	#if (RTL8822C_SUPPORT || RTL8812F_SUPPORT || RTL8197G_SUPPORT)
	if (dm->support_ic_type &
	   (ODM_RTL8822C | ODM_RTL8812F | ODM_RTL8197G)) {
		if (path > RF_PATH_B) {
			PHYDM_DBG(dm, ODM_PHY_CONFIG, "Unsupported path (%d)\n",
				  path);
			return false;
		}
		txagc_cck = (u8)odm_get_bb_reg(dm, r_txagc_cck[path],
						   0x7F0000);
		txagc_ofdm = (u8)odm_get_bb_reg(dm, r_txagc_ofdm[path],
						    0x1FC00);
		if (is_positive) {
			if (((txagc_cck + pwr_offset) > 127) ||
			    ((txagc_ofdm + pwr_offset) > 127))
				return false;

			txagc_cck += pwr_offset;
			txagc_ofdm += pwr_offset;
		} else {
			if (pwr_offset > txagc_cck || pwr_offset > txagc_ofdm)
				return false;

			txagc_cck -= pwr_offset;
			txagc_ofdm -= pwr_offset;
		}
		#if (RTL8822C_SUPPORT)
		ret = config_phydm_write_txagc_ref_8822c(dm, (u8)txagc_cck,
							 path, PDM_CCK);
		ret &= config_phydm_write_txagc_ref_8822c(dm, (u8)txagc_ofdm,
							 path, PDM_OFDM);
		#endif
		#if (RTL8812F_SUPPORT)
		ret = config_phydm_write_txagc_ref_8812f(dm, (u8)txagc_cck,
							 path, PDM_CCK);
		ret &= config_phydm_write_txagc_ref_8812f(dm, (u8)txagc_ofdm,
							 path, PDM_OFDM);
		#endif
		#if (RTL8197G_SUPPORT)
		ret = config_phydm_write_txagc_ref_8197g(dm, (u8)txagc_cck,
							 path, PDM_CCK);
		ret &= config_phydm_write_txagc_ref_8197g(dm, (u8)txagc_ofdm,
							 path, PDM_OFDM);
		#endif
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "%s: path-%d txagc_cck_ref=%x txagc_ofdm_ref=0x%x\n",
			  __func__, path, txagc_cck, txagc_ofdm);
	}
	#endif

	#if (RTL8198F_SUPPORT || RTL8814B_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8198F | ODM_RTL8814B)) {
		if (path > RF_PATH_D) {
			PHYDM_DBG(dm, ODM_PHY_CONFIG, "Unsupported path (%d)\n",
				  path);
			return false;
		}
		txagc_cck = (u8)odm_get_bb_reg(dm, r_txagc_cck[path],
						   0x7F0000);
		txagc_ofdm = (u8)odm_get_bb_reg(dm, r_txagc_ofdm[path],
						    0x1FC00);
		if (is_positive) {
			if (((txagc_cck + pwr_offset) > 127) ||
			    ((txagc_ofdm + pwr_offset) > 127))
				return false;

			txagc_cck += pwr_offset;
			txagc_ofdm += pwr_offset;
		} else {
			if (pwr_offset > txagc_cck || pwr_offset > txagc_ofdm)
				return false;

			txagc_cck -= pwr_offset;
			txagc_ofdm -= pwr_offset;
		}
		#if (RTL8198F_SUPPORT)
		ret = config_phydm_write_txagc_ref_8198f(dm, (u8)txagc_cck,
							 path, PDM_CCK);
		ret &= config_phydm_write_txagc_ref_8198f(dm, (u8)txagc_ofdm,
							 path, PDM_OFDM);
		#endif
		#if (RTL8814B_SUPPORT)
		ret = config_phydm_write_txagc_ref_8814b(dm, (u8)txagc_cck,
							 path, PDM_CCK);
		ret &= config_phydm_write_txagc_ref_8814b(dm, (u8)txagc_ofdm,
							 path, PDM_OFDM);
		#endif
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "%s: path-%d txagc_cck_ref=%x txagc_ofdm_ref=0x%x\n",
			  __func__, path, txagc_cck, txagc_ofdm);
	}
	#endif

	#if (RTL8723F_SUPPORT)
	if (dm->support_ic_type & (ODM_RTL8723F)) {
		if (path > RF_PATH_A) {
			PHYDM_DBG(dm, ODM_PHY_CONFIG, "Unsupported path (%d)\n",
				  path);
			return false;
		}
		txagc_cck = (u8)odm_get_bb_reg(dm, r_txagc_cck[path],
						   0x7F0000);
		txagc_ofdm = (u8)odm_get_bb_reg(dm, r_txagc_ofdm[path],
						    0x1FC00);
		if (is_positive) {
			if (((txagc_cck + pwr_offset) > 127) ||
			    ((txagc_ofdm + pwr_offset) > 127))
				return false;

			txagc_cck += pwr_offset;
			txagc_ofdm += pwr_offset;
		} else {
			if (pwr_offset > txagc_cck || pwr_offset > txagc_ofdm)
				return false;

			txagc_cck -= pwr_offset;
			txagc_ofdm -= pwr_offset;
		}
		#if (RTL8723F_SUPPORT)
		ret = config_phydm_write_txagc_ref_8723f(dm, (u8)txagc_cck,
							 path, PDM_CCK);
		ret &= config_phydm_write_txagc_ref_8723f(dm, (u8)txagc_ofdm,
							 path, PDM_OFDM);
		#endif
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "%s: path-%d txagc_cck_ref=%x txagc_ofdm_ref=0x%x\n",
			  __func__, path, txagc_cck, txagc_ofdm);
	}
	#endif

	return ret;
}

boolean
phydm_api_set_txagc(void *dm_void, u32 pwr_idx, enum rf_path path,
		    u8 rate, boolean is_single_rate)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean ret = false;
	#if (RTL8198F_SUPPORT || RTL8822C_SUPPORT || RTL8812F_SUPPORT ||\
	     RTL8814B_SUPPORT || RTL8197G_SUPPORT || RTL8723F_SUPPORT)
	u8 base = 0;
	u8 txagc_tmp = 0;
	s8 pw_by_rate_tmp = 0;
	s8 pw_by_rate_new = 0;
	#endif
	#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	u8 i = 0;
	#endif

#if (RTL8822B_SUPPORT || RTL8821C_SUPPORT || RTL8195B_SUPPORT)
	if (dm->support_ic_type &
	    (ODM_RTL8822B | ODM_RTL8821C | ODM_RTL8195B)) {
		if (is_single_rate) {
			#if (RTL8822B_SUPPORT)
			if (dm->support_ic_type == ODM_RTL8822B)
				ret = phydm_write_txagc_1byte_8822b(dm, pwr_idx,
								    path, rate);
			#endif

			#if (RTL8821C_SUPPORT)
			if (dm->support_ic_type == ODM_RTL8821C)
				ret = phydm_write_txagc_1byte_8821c(dm, pwr_idx,
								    path, rate);
			#endif

			#if (RTL8195B_SUPPORT)
			if (dm->support_ic_type == ODM_RTL8195B)
				ret = phydm_write_txagc_1byte_8195b(dm, pwr_idx,
								    path, rate);
			#endif

			#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
			set_current_tx_agc(dm->priv, path, rate, (u8)pwr_idx);
			#endif

		} else {
			#if (RTL8822B_SUPPORT)
			if (dm->support_ic_type == ODM_RTL8822B)
				ret = config_phydm_write_txagc_8822b(dm,
								     pwr_idx,
								     path,
								     rate);
			#endif

			#if (RTL8821C_SUPPORT)
			if (dm->support_ic_type == ODM_RTL8821C)
				ret = config_phydm_write_txagc_8821c(dm,
								     pwr_idx,
								     path,
								     rate);
			#endif

			#if (RTL8195B_SUPPORT)
			if (dm->support_ic_type == ODM_RTL8195B)
				ret = config_phydm_write_txagc_8195b(dm,
								     pwr_idx,
								     path,
								     rate);
			#endif

			#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
			for (i = 0; i < 4; i++)
				set_current_tx_agc(dm->priv, path, (rate + i),
						   (u8)pwr_idx);
			#endif
		}
	}
#endif

#if (RTL8198F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8198F) {
		if (rate < 0x4)
			txagc_tmp = config_phydm_read_txagc_8198f(dm, path,
								  rate,
								  PDM_CCK);
		else
			txagc_tmp = config_phydm_read_txagc_8198f(dm, path,
								  rate,
								  PDM_OFDM);

		pw_by_rate_tmp = config_phydm_read_txagc_diff_8198f(dm, rate);
		base = txagc_tmp -  pw_by_rate_tmp;
		base = base & 0x7f;
		if (DIFF_2((pwr_idx & 0x7f), base) > 64 || pwr_idx > 127)
			return false;

		pw_by_rate_new = (s8)(pwr_idx - base);
		ret = phydm_write_txagc_1byte_8198f(dm, pw_by_rate_new, rate);
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "%s: path-%d rate_idx=%x base=0x%x new_diff=0x%x\n",
			  __func__, path, rate, base, pw_by_rate_new);
	}
#endif

#if (RTL8822C_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8822C) {
		if (rate < 0x4)
			txagc_tmp = config_phydm_read_txagc_8822c(dm, path,
								  rate,
								  PDM_CCK);
		else
			txagc_tmp = config_phydm_read_txagc_8822c(dm, path,
								  rate,
								  PDM_OFDM);

		pw_by_rate_tmp = config_phydm_read_txagc_diff_8822c(dm, rate);
		base = txagc_tmp - pw_by_rate_tmp;
		base = base & 0x7f;
		if (DIFF_2((pwr_idx & 0x7f), base) > 63 || pwr_idx > 127)
			return false;

		pw_by_rate_new = (s8)(pwr_idx - base);
		ret = phydm_write_txagc_1byte_8822c(dm, pw_by_rate_new, rate);
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "%s: path-%d rate_idx=%x base=0x%x new_diff=0x%x\n",
			  __func__, path, rate, base, pw_by_rate_new);
	}
#endif

#if (RTL8814B_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8814B) {
		if (rate < 0x4)
			txagc_tmp = config_phydm_read_txagc_8814b(dm, path,
								  rate,
								  PDM_CCK);
		else
			txagc_tmp = config_phydm_read_txagc_8814b(dm, path,
								  rate,
								  PDM_OFDM);

		pw_by_rate_tmp = config_phydm_read_txagc_diff_8814b(dm, rate);
		base = txagc_tmp -  pw_by_rate_tmp;
		base = base & 0x7f;
		if (DIFF_2((pwr_idx & 0x7f), base) > 64)
			return false;

		pw_by_rate_new = (s8)(pwr_idx - base);
		ret = phydm_write_txagc_1byte_8814b(dm, pw_by_rate_new, rate);
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "%s: path-%d rate_idx=%x base=0x%x new_diff=0x%x\n",
			  __func__, path, rate, base, pw_by_rate_new);
	}
#endif

#if (RTL8812F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8812F) {
		if (rate < 0x4)
			txagc_tmp = config_phydm_read_txagc_8812f(dm, path,
								  rate,
								  PDM_CCK);
		else
			txagc_tmp = config_phydm_read_txagc_8812f(dm, path,
								  rate,
								  PDM_OFDM);

		pw_by_rate_tmp = config_phydm_read_txagc_diff_8812f(dm, rate);
		base = txagc_tmp - pw_by_rate_tmp;
		base = base & 0x7f;
		if (DIFF_2((pwr_idx & 0x7f), base) > 63 || pwr_idx > 127)
			return false;

		pw_by_rate_new = (s8)(pwr_idx - base);
		ret = phydm_write_txagc_1byte_8812f(dm, pw_by_rate_new, rate);
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "%s: path-%d rate_idx=%x base=0x%x new_diff=0x%x\n",
			  __func__, path, rate, base, pw_by_rate_new);
	}
#endif

#if (RTL8197G_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8197G) {
		if (rate < 0x4)
			txagc_tmp = config_phydm_read_txagc_8197g(dm, path,
								  rate,
								  PDM_CCK);
		else
			txagc_tmp = config_phydm_read_txagc_8197g(dm, path,
								  rate,
								  PDM_OFDM);

		pw_by_rate_tmp = config_phydm_read_txagc_diff_8197g(dm, rate);
		base = txagc_tmp - pw_by_rate_tmp;
		base = base & 0x7f;
		if (DIFF_2((pwr_idx & 0x7f), base) > 63 || pwr_idx > 127)
			return false;

		pw_by_rate_new = (s8)(pwr_idx - base);
		ret = phydm_write_txagc_1byte_8197g(dm, pw_by_rate_new, rate);
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "%s: path-%d rate_idx=%x base=0x%x new_diff=0x%x\n",
			  __func__, path, rate, base, pw_by_rate_new);
	}
#endif
#if (RTL8723F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8723F) {
		if (rate < 0x4)
			txagc_tmp = config_phydm_read_txagc_8723f(dm, path,
								  rate,
								  PDM_CCK);
		else
			txagc_tmp = config_phydm_read_txagc_8723f(dm, path,
								  rate,
								  PDM_OFDM);

		pw_by_rate_tmp = config_phydm_read_txagc_diff_8723f(dm, rate);
		base = txagc_tmp - pw_by_rate_tmp;
		base = base & 0x7f;
		if (DIFF_2((pwr_idx & 0x7f), base) > 63 || pwr_idx > 127)
			return false;

		pw_by_rate_new = (s8)(pwr_idx - base);
		ret = phydm_write_txagc_1byte_8723f(dm, pw_by_rate_new, rate);
		PHYDM_DBG(dm, ODM_PHY_CONFIG,
			  "%s: path-%d rate_idx=%x base=0x%x new_diff=0x%x\n",
			  __func__, path, rate, base, pw_by_rate_new);
	}
#endif

#if (RTL8197F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8197F)
		ret = config_phydm_write_txagc_8197f(dm, pwr_idx, path, rate);
#endif

#if (RTL8192F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8192F)
		ret = config_phydm_write_txagc_8192f(dm, pwr_idx, path, rate);
#endif

#if (RTL8721D_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8721D)
		ret = config_phydm_write_txagc_8721d(dm, pwr_idx, path, rate);
#endif
#if (RTL8710C_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8710C)
		ret = config_phydm_write_txagc_8710c(dm, pwr_idx, path, rate);
#endif
	return ret;
}

u8 phydm_api_get_txagc(void *dm_void, enum rf_path path, u8 hw_rate)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 ret = 0;

#if (RTL8822B_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8822B)
		ret = config_phydm_read_txagc_8822b(dm, path, hw_rate);
#endif

#if (RTL8197F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8197F)
		ret = config_phydm_read_txagc_8197f(dm, path, hw_rate);
#endif

#if (RTL8821C_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8821C)
		ret = config_phydm_read_txagc_8821c(dm, path, hw_rate);
#endif

#if (RTL8195B_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8195B)
		ret = config_phydm_read_txagc_8195b(dm, path, hw_rate);
#endif

/*@jj add 20170822*/
#if (RTL8192F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8192F)
		ret = config_phydm_read_txagc_8192f(dm, path, hw_rate);
#endif

#if (RTL8198F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8198F) {
		if (hw_rate < 0x4) {
			ret = config_phydm_read_txagc_8198f(dm, path, hw_rate,
							    PDM_CCK);
		} else {
			ret = config_phydm_read_txagc_8198f(dm, path, hw_rate,
							    PDM_OFDM);
		}
	}
#endif

#if (RTL8822C_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8822C) {
		if (hw_rate < 0x4) {
			ret = config_phydm_read_txagc_8822c(dm, path, hw_rate,
							    PDM_CCK);
		} else {
			ret = config_phydm_read_txagc_8822c(dm, path, hw_rate,
							    PDM_OFDM);
		}
	}
#endif

#if (RTL8723F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8723F) {
		if (hw_rate < 0x4) {
			ret = config_phydm_read_txagc_8723f(dm, path, hw_rate,
							    PDM_CCK);
		} else {
			ret = config_phydm_read_txagc_8723f(dm, path, hw_rate,
							    PDM_OFDM);
		}
	}
#endif

#if (RTL8814B_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8814B) {
		if (hw_rate < 0x4) {
			ret = config_phydm_read_txagc_8814b(dm, path, hw_rate,
							    PDM_CCK);
		} else {
			ret = config_phydm_read_txagc_8814b(dm, path, hw_rate,
							    PDM_OFDM);
		}
	}
#endif

#if (RTL8812F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8812F) {
		if (hw_rate < 0x4) {
			ret = config_phydm_read_txagc_8812f(dm, path, hw_rate,
							    PDM_CCK);
		} else {
			ret = config_phydm_read_txagc_8812f(dm, path, hw_rate,
							    PDM_OFDM);
		}
	}
#endif

#if (RTL8197G_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8197G) {
		if (hw_rate < 0x4) {
			ret = config_phydm_read_txagc_8197g(dm, path,
							    hw_rate,
							    PDM_CCK);
		} else {
			ret = config_phydm_read_txagc_8197g(dm, path,
							    hw_rate,
							    PDM_OFDM);
		}
	}
#endif

#if (RTL8721D_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8721D)
		ret = config_phydm_read_txagc_8721d(dm, path, hw_rate);
#endif
#if (RTL8710C_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8710C)
		ret = config_phydm_read_txagc_8710c(dm, path, hw_rate);
#endif
	return ret;
}

#if (RTL8822C_SUPPORT)
void phydm_shift_rxagc_table(void *dm_void, boolean is_pos_shift, u8 sft)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i = 0;
	u8 j = 0;
	u32 reg = 0;
	u16 max_rf_gain = 0;
	u16 min_rf_gain = 0;

	dm->is_agc_tab_pos_shift = is_pos_shift;
	dm->agc_table_shift = sft;

	for (i = 0; i <= dm->agc_table_cnt; i++) {
		max_rf_gain = dm->agc_rf_gain_ori[i][0];
		min_rf_gain = dm->agc_rf_gain_ori[i][63];

		if (dm->support_ic_type & ODM_RTL8822C)
			dm->l_bnd_detect[i] = false;

		for (j = 0; j < 64; j++) {
			if (is_pos_shift) {
				if (j < sft)
					reg = (max_rf_gain & 0x3ff);
				else
					reg = (dm->agc_rf_gain_ori[i][j - sft] &
						 0x3ff);
			} else {
				if (j > 63 - sft)
					reg = (min_rf_gain & 0x3ff);

				else
					reg = (dm->agc_rf_gain_ori[i][j + sft] &
						 0x3ff);
			}
			dm->agc_rf_gain[i][j] = (u16)(reg & 0x3ff);

			reg |= (j & 0x3f) << 16;/*mp_gain_idx*/
			reg |= (i & 0xf) << 22;/*table*/
			reg |= BIT(29) | BIT(28);/*write en*/
			odm_set_bb_reg(dm, R_0x1d90, MASKDWORD, reg);
		}
	}

	if (dm->support_ic_type & ODM_RTL8822C)
		odm_set_bb_reg(dm, R_0x828, 0xf8, L_BND_DEFAULT_8822C);
}
#endif

boolean
phydm_api_switch_bw_channel(void *dm_void, u8 ch, u8 pri_ch,
			    enum channel_width bw)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean ret = false;

	switch (dm->support_ic_type) {
#if (RTL8822B_SUPPORT)
	case ODM_RTL8822B:
		ret = config_phydm_switch_channel_bw_8822b(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8197F_SUPPORT)
	case ODM_RTL8197F:
		ret = config_phydm_switch_channel_bw_8197f(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8821C_SUPPORT)
	case ODM_RTL8821C:
		ret = config_phydm_switch_channel_bw_8821c(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8195B_SUPPORT)
	case ODM_RTL8195B:
		ret = config_phydm_switch_channel_bw_8195b(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8192F_SUPPORT)
	case ODM_RTL8192F:
		ret = config_phydm_switch_channel_bw_8192f(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8198F_SUPPORT)
	case ODM_RTL8198F:
		ret = config_phydm_switch_channel_bw_8198f(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8822C_SUPPORT)
	case ODM_RTL8822C:
		ret = config_phydm_switch_channel_bw_8822c(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8723F_SUPPORT)
	case ODM_RTL8723F:
		ret = config_phydm_switch_channel_bw_8723f(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8814B_SUPPORT)
	case ODM_RTL8814B:
		ret = config_phydm_switch_channel_bw_8814b(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8812F_SUPPORT)
	case ODM_RTL8812F:
		ret = config_phydm_switch_channel_bw_8812f(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8197G_SUPPORT)
	case ODM_RTL8197G:
		ret = config_phydm_switch_channel_bw_8197g(dm, ch, pri_ch, bw);
	break;
#endif

#if (RTL8721D_SUPPORT)
	case ODM_RTL8721D:
		ret = config_phydm_switch_channel_bw_8721d(dm, ch, pri_ch, bw);
	break;
#endif
#if (RTL8710C_SUPPORT)
	case ODM_RTL8710C:
		ret = config_phydm_switch_channel_bw_8710c(dm, ch, pri_ch, bw);
	break;
#endif

	default:
		break;
	}
	return ret;
}

boolean
phydm_api_trx_mode(void *dm_void, enum bb_path tx_path, enum bb_path rx_path,
		   enum bb_path tx_path_ctrl)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	boolean ret = false;
	boolean is_2tx = false;

	if (tx_path_ctrl == BB_PATH_AB)
		is_2tx = true;

	switch (dm->support_ic_type) {
	#if (RTL8822B_SUPPORT)
	case ODM_RTL8822B:
		ret = config_phydm_trx_mode_8822b(dm, tx_path, rx_path,
						  tx_path_ctrl);
		break;
	#endif

	#if (RTL8197F_SUPPORT)
	case ODM_RTL8197F:
		ret = config_phydm_trx_mode_8197f(dm, tx_path, rx_path, is_2tx);
		break;
	#endif

	#if (RTL8192F_SUPPORT)
	case ODM_RTL8192F:
		ret = config_phydm_trx_mode_8192f(dm, tx_path, rx_path,
						  tx_path_ctrl);
		break;
	#endif

	#if (RTL8198F_SUPPORT)
	case ODM_RTL8198F:
		ret = config_phydm_trx_mode_8198f(dm, tx_path, rx_path, is_2tx);
		break;
	#endif

	#if (RTL8814B_SUPPORT)
	case ODM_RTL8814B:
		ret = config_phydm_trx_mode_8814b(dm, tx_path, rx_path);
		break;
	#endif

	#if (RTL8822C_SUPPORT)
	case ODM_RTL8822C:
		ret = config_phydm_trx_mode_8822c(dm, tx_path, rx_path,
						  tx_path_ctrl);
		break;
	#endif

	#if (RTL8812F_SUPPORT)
	case ODM_RTL8812F:
		ret = config_phydm_trx_mode_8812f(dm, tx_path, rx_path, is_2tx);
		break;
	#endif

	#if (RTL8197G_SUPPORT)
	case ODM_RTL8197G:
		ret = config_phydm_trx_mode_8197g(dm, tx_path, rx_path, is_2tx);
		break;
	#endif

	#if (RTL8721D_SUPPORT)
	case ODM_RTL8721D:
		ret = config_phydm_trx_mode_8721d(dm, tx_path, rx_path, is_2tx);
		break;
	#endif

	#if (RTL8710C_SUPPORT)
	case ODM_RTL8710C:
		ret = config_phydm_trx_mode_8710c(dm, tx_path, rx_path, is_2tx);
		break;
	#endif
	}
	return ret;
}
#endif

#ifdef PHYDM_COMMON_API_NOT_SUPPORT
u8 config_phydm_read_txagc_n(void *dm_void, enum rf_path path, u8 hw_rate)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 read_back_data = INVALID_TXAGC_DATA;
	u32 reg_txagc;
	u32 reg_mask;
	/* This function is for 92E/88E etc... */
	/* @Input need to be HW rate index, not driver rate index!!!! */

	/* @Error handling */
	if (path > RF_PATH_B || hw_rate > ODM_RATEMCS15) {
		PHYDM_DBG(dm, ODM_PHY_CONFIG, "%s: unsupported path (%d)\n",
			  __func__, path);
		return INVALID_TXAGC_DATA;
	}

	if (path == RF_PATH_A) {
		switch (hw_rate) {
		case ODM_RATE1M:
			reg_txagc = R_0xe08;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATE2M:
			reg_txagc = R_0x86c;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATE5_5M:
			reg_txagc = R_0x86c;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATE11M:
			reg_txagc = R_0x86c;
			reg_mask = 0x7f000000;
			break;

		case ODM_RATE6M:
			reg_txagc = R_0xe00;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATE9M:
			reg_txagc = R_0xe00;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATE12M:
			reg_txagc = R_0xe00;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATE18M:
			reg_txagc = R_0xe00;
			reg_mask = 0x7f000000;
			break;
		case ODM_RATE24M:
			reg_txagc = R_0xe04;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATE36M:
			reg_txagc = R_0xe04;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATE48M:
			reg_txagc = R_0xe04;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATE54M:
			reg_txagc = R_0xe04;
			reg_mask = 0x7f000000;
			break;

		case ODM_RATEMCS0:
			reg_txagc = R_0xe10;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATEMCS1:
			reg_txagc = R_0xe10;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATEMCS2:
			reg_txagc = R_0xe10;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATEMCS3:
			reg_txagc = R_0xe10;
			reg_mask = 0x7f000000;
			break;
		case ODM_RATEMCS4:
			reg_txagc = R_0xe14;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATEMCS5:
			reg_txagc = R_0xe14;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATEMCS6:
			reg_txagc = R_0xe14;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATEMCS7:
			reg_txagc = R_0xe14;
			reg_mask = 0x7f000000;
			break;
		case ODM_RATEMCS8:
			reg_txagc = R_0xe18;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATEMCS9:
			reg_txagc = R_0xe18;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATEMCS10:
			reg_txagc = R_0xe18;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATEMCS11:
			reg_txagc = R_0xe18;
			reg_mask = 0x7f000000;
			break;
		case ODM_RATEMCS12:
			reg_txagc = R_0xe1c;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATEMCS13:
			reg_txagc = R_0xe1c;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATEMCS14:
			reg_txagc = R_0xe1c;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATEMCS15:
			reg_txagc = R_0xe1c;
			reg_mask = 0x7f000000;
			break;

		default:
			PHYDM_DBG(dm, ODM_PHY_CONFIG, "Invalid HWrate!\n");
			break;
		}
	} else if (path == RF_PATH_B) {
		switch (hw_rate) {
		case ODM_RATE1M:
			reg_txagc = R_0x838;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATE2M:
			reg_txagc = R_0x838;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATE5_5M:
			reg_txagc = R_0x838;
			reg_mask = 0x7f000000;
			break;
		case ODM_RATE11M:
			reg_txagc = R_0x86c;
			reg_mask = 0x0000007f;
			break;

		case ODM_RATE6M:
			reg_txagc = R_0x830;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATE9M:
			reg_txagc = R_0x830;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATE12M:
			reg_txagc = R_0x830;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATE18M:
			reg_txagc = R_0x830;
			reg_mask = 0x7f000000;
			break;
		case ODM_RATE24M:
			reg_txagc = R_0x834;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATE36M:
			reg_txagc = R_0x834;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATE48M:
			reg_txagc = R_0x834;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATE54M:
			reg_txagc = R_0x834;
			reg_mask = 0x7f000000;
			break;

		case ODM_RATEMCS0:
			reg_txagc = R_0x83c;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATEMCS1:
			reg_txagc = R_0x83c;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATEMCS2:
			reg_txagc = R_0x83c;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATEMCS3:
			reg_txagc = R_0x83c;
			reg_mask = 0x7f000000;
			break;
		case ODM_RATEMCS4:
			reg_txagc = R_0x848;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATEMCS5:
			reg_txagc = R_0x848;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATEMCS6:
			reg_txagc = R_0x848;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATEMCS7:
			reg_txagc = R_0x848;
			reg_mask = 0x7f000000;
			break;

		case ODM_RATEMCS8:
			reg_txagc = R_0x84c;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATEMCS9:
			reg_txagc = R_0x84c;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATEMCS10:
			reg_txagc = R_0x84c;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATEMCS11:
			reg_txagc = R_0x84c;
			reg_mask = 0x7f000000;
			break;
		case ODM_RATEMCS12:
			reg_txagc = R_0x868;
			reg_mask = 0x0000007f;
			break;
		case ODM_RATEMCS13:
			reg_txagc = R_0x868;
			reg_mask = 0x00007f00;
			break;
		case ODM_RATEMCS14:
			reg_txagc = R_0x868;
			reg_mask = 0x007f0000;
			break;
		case ODM_RATEMCS15:
			reg_txagc = R_0x868;
			reg_mask = 0x7f000000;
			break;

		default:
			PHYDM_DBG(dm, ODM_PHY_CONFIG, "Invalid HWrate!\n");
			break;
		}
	} else {
		PHYDM_DBG(dm, ODM_PHY_CONFIG, "Invalid RF path!!\n");
	}
	read_back_data = (u8)odm_get_bb_reg(dm, reg_txagc, reg_mask);
	PHYDM_DBG(dm, ODM_PHY_CONFIG, "%s: path-%d rate index 0x%x = 0x%x\n",
		  __func__, path, hw_rate, read_back_data);
	return read_back_data;
}
#endif

#ifdef CONFIG_MCC_DM
#ifdef DYN_ANT_WEIGHTING_SUPPORT
void phydm_set_weighting_cmn(struct dm_struct *dm)
{
	PHYDM_DBG(dm, DBG_COMP_MCC, "%s\n", __func__);
	odm_set_bb_reg(dm, 0xc04, (BIT(18) | BIT(21)), 0x0);
	odm_set_bb_reg(dm, 0xe04, (BIT(18) | BIT(21)), 0x0);
}

void phydm_set_weighting_mcc(u8 b_equal_weighting, void *dm_void, u8 port)
{
	/*u8 reg_8;*/
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;
	u8	val_0x98e, val_0x98f, val_0x81b;
	u32 temp_reg;

	PHYDM_DBG(dm, DBG_COMP_MCC, "ant_weighting_mcc, port = %d\n", port);
	if (b_equal_weighting) {
		temp_reg = odm_get_bb_reg(dm, 0x98c, 0x00ff0000);
		val_0x98e = (u8)(temp_reg >> 16) & 0xc0;
		temp_reg = odm_get_bb_reg(dm, 0x98c, 0xff000000);
		val_0x98f = (u8)(temp_reg >> 24) & 0x7f;
		temp_reg = odm_get_bb_reg(dm, 0x818, 0xff000000);
		val_0x81b = (u8)(temp_reg >> 24) & 0xfd;
		PHYDM_DBG(dm, DBG_COMP_MCC, "Equal weighting ,rssi_min = %d\n",
			  dm->rssi_min);
		/*equal weighting*/
	} else {
		val_0x98e = 0x44;
		val_0x98f = 0x43;
		temp_reg = odm_get_bb_reg(dm, 0x818, 0xff000000);
		val_0x81b = (u8)(temp_reg >> 24) | BIT(2);
		PHYDM_DBG(dm, DBG_COMP_MCC, "AGC weighting ,rssi_min = %d\n",
			  dm->rssi_min);
		/*fix sec_min_wgt = 1/2*/
	}
	mcc_dm->mcc_reg_id[2] = 0x2;
	mcc_dm->mcc_dm_reg[2] = 0x98e;
	mcc_dm->mcc_dm_val[2][port] = val_0x98e;

	mcc_dm->mcc_reg_id[3] = 0x3;
	mcc_dm->mcc_dm_reg[3] = 0x98f;
	mcc_dm->mcc_dm_val[3][port] = val_0x98f;

	mcc_dm->mcc_reg_id[4] = 0x4;
	mcc_dm->mcc_dm_reg[4] = 0x81b;
	mcc_dm->mcc_dm_val[4][port] = val_0x81b;
}

void phydm_dyn_ant_dec_mcc(u8 port, u8 rssi_in, void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 rssi_l2h = 43, rssi_h2l = 37;

	if (rssi_in == 0xff)
		phydm_set_weighting_mcc(FALSE, dm, port);
	else if (rssi_in >= rssi_l2h)
		phydm_set_weighting_mcc(TRUE, dm, port);
	else if (rssi_in <= rssi_h2l)
		phydm_set_weighting_mcc(FALSE, dm, port);
}

void phydm_dynamic_ant_weighting_mcc_8822b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;
	u8	i;

	phydm_set_weighting_cmn(dm);
	for (i = 0; i <= 1; i++)
		phydm_dyn_ant_dec_mcc(i, mcc_dm->mcc_rssi[i], dm);
}
#endif /*#ifdef DYN_ANT_WEIGHTING_SUPPORT*/

void phydm_mcc_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;
	u8	i;

	/*PHYDM_DBG(dm, DBG_COMP_MCC, ("MCC init\n"));*/
	PHYDM_DBG(dm, DBG_COMP_MCC, "MCC init\n");
	for (i = 0; i < MCC_DM_REG_NUM; i++) {
		mcc_dm->mcc_reg_id[i] = 0xff;
		mcc_dm->mcc_dm_reg[i] = 0;
		mcc_dm->mcc_dm_val[i][0] = 0;
		mcc_dm->mcc_dm_val[i][1] = 0;
	}
	for (i = 0; i < NUM_STA; i++) {
		mcc_dm->sta_macid[0][i] = 0xff;
		mcc_dm->sta_macid[1][i] = 0xff;
	}
	/* Function init */
	dm->is_stop_dym_ant_weighting = 0;
}

u8 phydm_check(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;
	struct cmn_sta_info			*p_entry = NULL;
	u8	shift = 0;
	u8	i = 0;
	u8	j = 0;
	u8	rssi_min[2] = {0xff, 0xff};
	u8	sta_num = 8;
	u8 mcc_macid = 0;

	for (i = 0; i <= 1; i++) {
		for (j = 0; j < sta_num; j++) {
			if (mcc_dm->sta_macid[i][j] != 0xff) {
				mcc_macid = mcc_dm->sta_macid[i][j];
				p_entry = dm->phydm_sta_info[mcc_macid];
				if (!p_entry) {
					PHYDM_DBG(dm, DBG_COMP_MCC,
						  "PEntry NULL(mac=%d)\n",
						  mcc_dm->sta_macid[i][j]);
					return _FAIL;
				}
				PHYDM_DBG(dm, DBG_COMP_MCC,
					  "undec_smoothed_pwdb=%d\n",
					  p_entry->rssi_stat.rssi);
				if (p_entry->rssi_stat.rssi < rssi_min[i])
					rssi_min[i] = p_entry->rssi_stat.rssi;
			}
		}
	}
	mcc_dm->mcc_rssi[0] = (u8)rssi_min[0];
	mcc_dm->mcc_rssi[1] = (u8)rssi_min[1];
	return _SUCCESS;
}

void phydm_mcc_h2ccmd_rst(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;
	u8 i;
	u8 regid;
	u8 h2c_mcc[H2C_MAX_LENGTH];

	/* RST MCC */
	for (i = 0; i < H2C_MAX_LENGTH; i++)
		h2c_mcc[i] = 0xff;
	h2c_mcc[0] = 0x00;
	odm_fill_h2c_cmd(dm, PHYDM_H2C_MCC, H2C_MAX_LENGTH, h2c_mcc);
	PHYDM_DBG(dm, DBG_COMP_MCC, "MCC H2C RST\n");
}

void phydm_mcc_h2ccmd(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;
	u8 i;
	u8 regid;
	u8 h2c_mcc[H2C_MAX_LENGTH];

	if (mcc_dm->mcc_rf_ch[0] == 0xff && mcc_dm->mcc_rf_ch[1] == 0xff) {
		PHYDM_DBG(dm, DBG_COMP_MCC, "MCC channel Error\n");
		return;
	}
	/* Set Channel number */
	for (i = 0; i < H2C_MAX_LENGTH; i++)
		h2c_mcc[i] = 0xff;
	h2c_mcc[0] = 0xe0;
	h2c_mcc[1] = (u8)(mcc_dm->mcc_rf_ch[0]);
	h2c_mcc[2] = (u8)(mcc_dm->mcc_rf_ch[0] >> 8);
	h2c_mcc[3] = (u8)(mcc_dm->mcc_rf_ch[1]);
	h2c_mcc[4] = (u8)(mcc_dm->mcc_rf_ch[1] >> 8);
	h2c_mcc[5] = 0xff;
	h2c_mcc[6] = 0xff;
	odm_fill_h2c_cmd(dm, PHYDM_H2C_MCC, H2C_MAX_LENGTH, h2c_mcc);
	PHYDM_DBG(dm, DBG_COMP_MCC,
		  "MCC H2C SetCH: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
		  h2c_mcc[0], h2c_mcc[1], h2c_mcc[2], h2c_mcc[3],
		  h2c_mcc[4], h2c_mcc[5], h2c_mcc[6]);

	/* Set Reg and value*/
	for (i = 0; i < H2C_MAX_LENGTH; i++)
		h2c_mcc[i] = 0xff;

	for (i = 0; i < MCC_DM_REG_NUM; i++) {
		regid = mcc_dm->mcc_reg_id[i];
		if (regid != 0xff) {
			h2c_mcc[0] = 0xa0 | (regid & 0x1f);
			h2c_mcc[1] = (u8)(mcc_dm->mcc_dm_reg[i]);
			h2c_mcc[2] = (u8)(mcc_dm->mcc_dm_reg[i] >> 8);
			h2c_mcc[3] = mcc_dm->mcc_dm_val[i][0];
			h2c_mcc[4] = mcc_dm->mcc_dm_val[i][1];
			h2c_mcc[5] = 0xff;
			h2c_mcc[6] = 0xff;
			odm_fill_h2c_cmd(dm, PHYDM_H2C_MCC, H2C_MAX_LENGTH,
					 h2c_mcc);
			PHYDM_DBG(dm, DBG_COMP_MCC,
				  "MCC H2C: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
				  h2c_mcc[0], h2c_mcc[1], h2c_mcc[2],
				  h2c_mcc[3], h2c_mcc[4],
				  h2c_mcc[5], h2c_mcc[6]);
		}
	}
}

void phydm_mcc_ctrl(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;

	PHYDM_DBG(dm, DBG_COMP_MCC, "MCC status: %x\n", mcc_dm->mcc_status);
	/*MCC stage no change*/
	if (mcc_dm->mcc_status == mcc_dm->mcc_pre_status)
		return;
	/*Not in MCC stage*/
	if (mcc_dm->mcc_status == 0) {
		/* Enable normal Ant-weighting */
		dm->is_stop_dym_ant_weighting = 0;
		/* Enable normal DIG */
		odm_pause_dig(dm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_1, 0x20);
	} else {
		/* Disable normal Ant-weighting */
		dm->is_stop_dym_ant_weighting = 1;
		/* Enable normal DIG */
		odm_pause_dig(dm, PHYDM_PAUSE_NO_SET, PHYDM_PAUSE_LEVEL_1,
			      0x20);
	}
	if (mcc_dm->mcc_status == 0 && mcc_dm->mcc_pre_status != 0)
		phydm_mcc_init(dm);
	mcc_dm->mcc_pre_status = mcc_dm->mcc_status;
	}

void phydm_fill_mcccmd(void *dm_void, u8 regid, u16 reg_add,
		       u8 val0, u8 val1)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;

	mcc_dm->mcc_reg_id[regid] = regid;
	mcc_dm->mcc_dm_reg[regid] = reg_add;
	mcc_dm->mcc_dm_val[regid][0] = val0;
	mcc_dm->mcc_dm_val[regid][1] = val1;
}

void phydm_mcc_switch(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _phydm_mcc_dm_ *mcc_dm = &dm->mcc_dm;
	s8 ret;

	phydm_mcc_ctrl(dm);
	if (mcc_dm->mcc_status == 0) {/*Not in MCC stage*/
		phydm_mcc_h2ccmd_rst(dm);
		return;
	}
	PHYDM_DBG(dm, DBG_COMP_MCC, "MCC switch\n");
	ret = phydm_check(dm);
	if (ret == _FAIL) {
		PHYDM_DBG(dm, DBG_COMP_MCC, "MCC check fail\n");
		return;
	}
	/* Set IGI*/
	phydm_mcc_igi_cal(dm);

	/* Set Antenna Gain*/
#if (RTL8822B_SUPPORT == 1)
	phydm_dynamic_ant_weighting_mcc_8822b(dm);
#endif
	/* Set H2C Cmd*/
	phydm_mcc_h2ccmd(dm);
}
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void phydm_normal_driver_rx_sniffer(
	struct dm_struct *dm,
	u8 *desc,
	PRT_RFD_STATUS rt_rfd_status,
	u8 *drv_info,
	u8 phy_status)
{
#if (defined(CONFIG_PHYDM_RX_SNIFFER_PARSING))
	u32 *msg;
	u16 seq_num;

	if (rt_rfd_status->packet_report_type != NORMAL_RX)
		return;

	if (!dm->is_linked) {
		if (rt_rfd_status->is_hw_error)
			return;
	}

	if (phy_status == true) {
		if (dm->rx_pkt_type == type_block_ack ||
		    dm->rx_pkt_type == type_rts || dm->rx_pkt_type == type_cts)
			seq_num = 0;
		else
			seq_num = rt_rfd_status->seq_num;

		PHYDM_DBG_F(dm, ODM_COMP_SNIFFER,
			    "%04d , %01s, rate=0x%02x, L=%04d , %s , %s",
			    seq_num,
			    /*rt_rfd_status->mac_id,*/
			    (rt_rfd_status->is_crc ? "C" :
			    rt_rfd_status->is_ampdu ? "A" : "_"),
			    rt_rfd_status->data_rate,
			    rt_rfd_status->length,
			    ((rt_rfd_status->band_width == 0) ? "20M" :
			    ((rt_rfd_status->band_width == 1) ? "40M" : "80M")),
			    (rt_rfd_status->is_ldpc ? "LDP" : "BCC"));

		if (dm->rx_pkt_type == type_asoc_req)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "AS_REQ");
		else if (dm->rx_pkt_type == type_asoc_rsp)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "AS_RSP");
		else if (dm->rx_pkt_type == type_probe_req)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "PR_REQ");
		else if (dm->rx_pkt_type == type_probe_rsp)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "PR_RSP");
		else if (dm->rx_pkt_type == type_deauth)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "DEAUTH");
		else if (dm->rx_pkt_type == type_beacon)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "BEACON");
		else if (dm->rx_pkt_type == type_block_ack_req)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "BA_REQ");
		else if (dm->rx_pkt_type == type_rts)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "__RTS_");
		else if (dm->rx_pkt_type == type_cts)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "__CTS_");
		else if (dm->rx_pkt_type == type_ack)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "__ACK_");
		else if (dm->rx_pkt_type == type_block_ack)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "__BA__");
		else if (dm->rx_pkt_type == type_data)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "_DATA_");
		else if (dm->rx_pkt_type == type_data_ack)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "Data_Ack");
		else if (dm->rx_pkt_type == type_qos_data)
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [%s]", "QoS_Data");
		else
			PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [0x%x]",
				    dm->rx_pkt_type);

		PHYDM_DBG_F(dm, ODM_COMP_SNIFFER, " , [RSSI=%d,%d,%d,%d ]",
			    dm->rssi_a,
			    dm->rssi_b,
			    dm->rssi_c,
			    dm->rssi_d);

		msg = (u32 *)drv_info;

		PHYDM_DBG_F(dm, ODM_COMP_SNIFFER,
			    " , P-STS[28:0]=%08x-%08x-%08x-%08x-%08x-%08x-%08x\n",
			    msg[6], msg[5], msg[4], msg[3],
			    msg[2], msg[1], msg[1]);
	} else {
		PHYDM_DBG_F(dm, ODM_COMP_SNIFFER,
			    "%04d , %01s, rate=0x%02x, L=%04d , %s , %s\n",
			    rt_rfd_status->seq_num,
			    /*rt_rfd_status->mac_id,*/
			    (rt_rfd_status->is_crc ? "C" :
			    (rt_rfd_status->is_ampdu) ? "A" : "_"),
			    rt_rfd_status->data_rate,
			    rt_rfd_status->length,
			    ((rt_rfd_status->band_width == 0) ? "20M" :
			    ((rt_rfd_status->band_width == 1) ? "40M" : "80M")),
			    (rt_rfd_status->is_ldpc ? "LDP" : "BCC"));
	}

#endif
}

#endif
