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
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef PHYDM_PMAC_TX_SETTING_SUPPORT
#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT

void phydm_start_cck_cont_tx_jgr3(void *dm_void,
				  struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;
	u8 rate = tx_info->tx_rate; /* HW rate */

	/* if CCK block on? */
	if (!odm_get_bb_reg(dm, R_0x1c3c, BIT(1)))
		odm_set_bb_reg(dm, R_0x1c3c, BIT(1), 1);

	/* Turn Off All Test mode */
	odm_set_bb_reg(dm, R_0x1ca4, 0x7, 0x0);

	odm_set_bb_reg(dm, R_0x1a00, 0x3000, rate);
	odm_set_bb_reg(dm, R_0x1a00, 0x3, 0x2); /* transmit mode */
	odm_set_bb_reg(dm, R_0x1a00, BIT(3), 0x1); /* turn on scramble*/

	/* Fix rate selection issue */
	odm_set_bb_reg(dm, R_0x1a70, BIT(14), 0x1);
	/* set RX weighting for path I & Q to 0 */
	odm_set_bb_reg(dm, R_0x1a14, 0x300, 0x3);
	/* set loopback mode */
	odm_set_bb_reg(dm, R_0x1c3c, BIT(4), 0x1);

	pmac_tx->cck_cont_tx = true;
	pmac_tx->ofdm_cont_tx = false;
}

void phydm_stop_cck_cont_tx_jgr3(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;

	pmac_tx->cck_cont_tx = false;
	pmac_tx->ofdm_cont_tx = false;

	odm_set_bb_reg(dm, R_0x1a00, 0x3, 0x0); /* normal mode */
	odm_set_bb_reg(dm, R_0x1a00, BIT(3), 0x1); /* turn on scramble*/

	/* back to default */
	odm_set_bb_reg(dm, R_0x1a70, BIT(14), 0x0);
	odm_set_bb_reg(dm, R_0x1a14, 0x300, 0x0);
	odm_set_bb_reg(dm, R_0x1c3c, BIT(4), 0x0);
	/* BB Reset */
	odm_set_bb_reg(dm, R_0x1d0c, BIT(16), 0x0);
	odm_set_bb_reg(dm, R_0x1d0c, BIT(16), 0x1);
}

void phydm_start_ofdm_cont_tx_jgr3(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;

	/* 1. if OFDM block on */
	if (!odm_get_bb_reg(dm, R_0x1c3c, BIT(0)))
		odm_set_bb_reg(dm, R_0x1c3c, BIT(0), 1);

	/* 2. set CCK test mode off, set to CCK normal mode */
	odm_set_bb_reg(dm, R_0x1a00, 0x3, 0);

	/* 3. turn on scramble setting */
	odm_set_bb_reg(dm, R_0x1a00, BIT(3), 1);

	/* 4. Turn On Continue Tx and turn off the other test modes. */
	odm_set_bb_reg(dm, R_0x1ca4, 0x7, 0x1);

	pmac_tx->cck_cont_tx = false;
	pmac_tx->ofdm_cont_tx = true;
}

void phydm_stop_ofdm_cont_tx_jgr3(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;

	pmac_tx->cck_cont_tx = false;
	pmac_tx->ofdm_cont_tx = false;

	/* Turn Off All Test mode */
	odm_set_bb_reg(dm, R_0x1ca4, 0x7, 0x0);

	/* Delay 10 ms */
	ODM_delay_ms(10);

	/* BB Reset */
	odm_set_bb_reg(dm, R_0x1d0c, BIT(16), 0x0);
	odm_set_bb_reg(dm, R_0x1d0c, BIT(16), 0x1);
}

void phydm_set_single_tone_jgr3(void *dm_void, boolean is_single_tone,
				boolean en_pmac_tx, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;
	u8 start = RF_PATH_A, end = RF_PATH_A;
	u8 i = 0;

	switch (path) {
	case RF_PATH_A:
	case RF_PATH_B:
	case RF_PATH_C:
	case RF_PATH_D:
		start = path;
		end = path;
		break;
	case RF_PATH_AB:
		start = RF_PATH_A;
		end = RF_PATH_B;
		break;
#if (RTL8814B_SUPPORT || RTL8198F_SUPPORT)
	case RF_PATH_AC:
		start = RF_PATH_A;
		end = RF_PATH_C;
		break;
	case RF_PATH_AD:
		start = RF_PATH_A;
		end = RF_PATH_D;
		break;
	case RF_PATH_BC:
		start = RF_PATH_B;
		end = RF_PATH_C;
		break;
	case RF_PATH_BD:
		start = RF_PATH_B;
		end = RF_PATH_D;
		break;
	case RF_PATH_CD:
		start = RF_PATH_C;
		end = RF_PATH_D;
		break;
	case RF_PATH_ABC:
		start = RF_PATH_A;
		end = RF_PATH_C;
		break;
	case RF_PATH_ABD:
		start = RF_PATH_A;
		end = RF_PATH_D;
		break;
	case RF_PATH_ACD:
		start = RF_PATH_A;
		end = RF_PATH_D;
		break;
	case RF_PATH_BCD:
		start = RF_PATH_B;
		end = RF_PATH_D;
		break;
	case RF_PATH_ABCD:
		start = RF_PATH_A;
		end = RF_PATH_D;
		break;
#endif
	}

	if (is_single_tone) {
		pmac_tx->tx_scailing = odm_get_bb_reg(dm, R_0x81c, MASKDWORD);

		if (!en_pmac_tx) {
			phydm_start_ofdm_cont_tx_jgr3(dm);
			/*SendPSPoll(pAdapter);*/
		}

		odm_set_bb_reg(dm, R_0x1c68, BIT(24), 0x1); /* Disable CCA */

		for (i = start; i <= end; i++) {
			/* Tx mode: RF0x00[19:16]=4'b0010 */
			/* odm_set_rf_reg(dm, i, RF_0x0, 0xF0000, 0x2); */
			/* Lowest RF gain index: RF_0x0[4:0] = 0*/
			odm_set_rf_reg(dm, i, RF_0x0, 0x1F, 0x0);
			/* RF LO enabled */
			odm_set_rf_reg(dm, i, RF_0x58, BIT(1), 0x1);
		}
		#if (RTL8814B_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8814B) {
			/* Tx mode: RF0x00[19:16]=4'b0010 */
			/* config_phydm_write_rf_syn_8814b(dm, RF_SYN0, RF_0x0,
			 *				0xF0000, 0x2);
			 */
			/* Lowest RF gain index: RF_0x0[4:0] = 0*/
			config_phydm_write_rf_syn_8814b(dm, RF_SYN0, RF_0x0,
							0x1F, 0x0);
			/* RF LO enabled */
			config_phydm_write_rf_syn_8814b(dm, RF_SYN0, RF_0x58,
							BIT(1), 0x1);
		}
		#endif
		odm_set_bb_reg(dm, R_0x81c, 0x001FC000, 0);
	} else {
		for (i = start; i <= end; i++) {
			/* RF LO disabled */
			odm_set_rf_reg(dm, i, RF_0x58, BIT(1), 0x0);
		}
		odm_set_bb_reg(dm, R_0x1c68, BIT(24), 0x0); /* Enable CCA */

		if (!en_pmac_tx)
			phydm_stop_ofdm_cont_tx_jgr3(dm);

		odm_set_bb_reg(dm, R_0x81c, MASKDWORD, pmac_tx->tx_scailing);
	}
}

void phydm_stop_pmac_tx_jgr3(void *dm_void, struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;
	u32 tmp = 0;

	if (tx_info->mode == CONT_TX) {
		odm_set_bb_reg(dm, R_0x1e70, 0xf, 0x2); /* TX Stop */
		if (pmac_tx->is_cck_rate)
			phydm_stop_cck_cont_tx_jgr3(dm);
		else
			phydm_stop_ofdm_cont_tx_jgr3(dm);
	} else {
		if (pmac_tx->is_cck_rate) {
			tmp = odm_get_bb_reg(dm, R_0x2de4, MASKLWORD);
			odm_set_bb_reg(dm, R_0x1e64, MASKLWORD, tmp + 50);
		}
		odm_set_bb_reg(dm, R_0x1e70, 0xf, 0x2); /* TX Stop */
	}

	if (tx_info->mode == OFDM_SINGLE_TONE_TX) {
		/* Stop HW TX -> Stop Continuous TX -> Stop RF Setting */
		if (pmac_tx->is_cck_rate)
			phydm_stop_cck_cont_tx_jgr3(dm);
		else
			phydm_stop_ofdm_cont_tx_jgr3(dm);

		phydm_set_single_tone_jgr3(dm, false, true, pmac_tx->path);
	}
}

void phydm_set_mac_phy_txinfo_jgr3(void *dm_void,
				   struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;
	u32 tmp = 0;

	odm_set_bb_reg(dm, R_0xa58, 0x003F8000, tx_info->tx_rate);

	/* 0x900[1] ndp_sound */
	odm_set_bb_reg(dm, R_0x900, BIT(1), tx_info->ndp_sound);

	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	tx_info->m_stbc = tx_info->m_stbc - 1;
	#endif

	/* 0x900[27:24] txsc [29:28] bw [31:30] m_stbc */
	tmp = (tx_info->tx_sc) | ((tx_info->bw) << 4) |
		((tx_info->m_stbc) << 6);

	if (tx_info->tx_sc == 1) /* upper*/
		odm_set_bb_reg(dm, R_0x1ae0, 0x7000, 0x5);
	else if (tx_info->tx_sc == 2) /* lower*/
		odm_set_bb_reg(dm, R_0x1ae0, 0x7000, 0x6);
	else /* duplicate*/
		odm_set_bb_reg(dm, R_0x1ae0, 0x7000, 0x0);

	odm_set_bb_reg(dm, R_0x900, 0xFF000000, tmp);

	if (pmac_tx->is_ofdm_rate) {
		odm_set_bb_reg(dm, R_0x900, BIT(0), 0);
		odm_set_bb_reg(dm, R_0x900, BIT(2), 0);
	} else if (pmac_tx->is_ht_rate) {
		odm_set_bb_reg(dm, R_0x900, BIT(0), 1);
		odm_set_bb_reg(dm, R_0x900, BIT(2), 0);
	} else if (pmac_tx->is_vht_rate) {
		odm_set_bb_reg(dm, R_0x900, BIT(0), 0);
		odm_set_bb_reg(dm, R_0x900, BIT(2), 1);
	}

	tmp = tx_info->packet_period; /* for TX interval */
	odm_set_bb_reg(dm, R_0x9b8, 0xffff0000, tmp);
}

void phydm_set_sig_jgr3(void *dm_void, struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;
	u32 tmp = 0;

	if (pmac_tx->is_cck_rate)
		return;

	/* L-SIG */
	odm_set_bb_reg(dm, R_0x1eb4, 0xfffff, tx_info->packet_count);

	tmp = BYTE_2_DWORD(0, tx_info->lsig[2], tx_info->lsig[1],
			   tx_info->lsig[0]);
	odm_set_bb_reg(dm, R_0x908, 0xffffff, tmp);
#if 0
	/* @0x924[7:0] = Data init octet */
	tmp = tx_info->packet_pattern;
	odm_set_bb_reg(dm, R_0x924, 0xff, tmp);

	if (tx_info->packet_pattern == RANDOM_BY_PN32)
		tmp = 0x3;
	else
		tmp = 0x0;

	odm_set_bb_reg(dm, R_0x914, 0x60000000, tmp);
#endif
	if (pmac_tx->is_ht_rate) {
	/* HT SIG */
		tmp = BYTE_2_DWORD(0, tx_info->ht_sig[2], tx_info->ht_sig[1],
				   tx_info->ht_sig[0]);
		odm_set_bb_reg(dm, R_0x90c, 0xffffff, tmp);
		tmp = BYTE_2_DWORD(0, tx_info->ht_sig[5], tx_info->ht_sig[4],
				   tx_info->ht_sig[3]);
		odm_set_bb_reg(dm, R_0x910, 0xffffff, tmp);
	} else if (pmac_tx->is_vht_rate) {
	/* VHT SIG A/B/serv_field/delimiter */
		tmp = BYTE_2_DWORD(0, tx_info->vht_sig_a[2],
				   tx_info->vht_sig_a[1],
				   tx_info->vht_sig_a[0]);
		odm_set_bb_reg(dm, R_0x90c, 0xffffff, tmp);
		tmp = BYTE_2_DWORD(0, tx_info->vht_sig_a[5],
				   tx_info->vht_sig_a[4],
				   tx_info->vht_sig_a[3]);
		odm_set_bb_reg(dm, R_0x910, 0xffffff, tmp);
		tmp = BYTE_2_DWORD(tx_info->vht_sig_b[3], tx_info->vht_sig_b[2],
				   tx_info->vht_sig_b[1],
				   tx_info->vht_sig_b[0]);
		odm_set_bb_reg(dm, R_0x914, 0x1FFFFFFF, tmp);

		tmp = tx_info->vht_sig_b_crc;
		odm_set_bb_reg(dm, R_0x938, 0xff00, tmp);

		tmp = BYTE_2_DWORD(tx_info->vht_delimiter[3],
				   tx_info->vht_delimiter[2],
				   tx_info->vht_delimiter[1],
				   tx_info->vht_delimiter[0]);
		odm_set_bb_reg(dm, R_0x940, MASKDWORD, tmp);
	}
}

void phydm_set_cck_preamble_hdr_jgr3(void *dm_void,
				     struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;
	u32 tmp = 0;

	if (!pmac_tx->is_cck_rate)
		return;

	tmp = tx_info->packet_count | (tx_info->sfd << 16);
	odm_set_bb_reg(dm, R_0x1e64, MASKDWORD, tmp);
	tmp = tx_info->signal_field | (tx_info->service_field << 8) |
	      (tx_info->length << 16);
	odm_set_bb_reg(dm, R_0x1e68, MASKDWORD, tmp);
	tmp = BYTE_2_DWORD(0, 0, tx_info->crc16[1], tx_info->crc16[0]);
	odm_set_bb_reg(dm, R_0x1e6c, 0xffff, tmp);

	if (tx_info->is_short_preamble)
		odm_set_bb_reg(dm, R_0x1e6c, BIT(16), 0);
	else
		odm_set_bb_reg(dm, R_0x1e6c, BIT(16), 1);
}

void phydm_set_mode_jgr3(void *dm_void, struct phydm_pmac_info *tx_info,
			 enum phydm_pmac_mode mode)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;

	if (mode == CONT_TX) {
		tx_info->packet_count = 1;

		if (pmac_tx->is_cck_rate)
			phydm_start_cck_cont_tx_jgr3(dm, tx_info);
		else
			phydm_start_ofdm_cont_tx_jgr3(dm);
	} else if (mode == OFDM_SINGLE_TONE_TX) {
		/* Continuous TX -> HW TX -> RF Setting */
		tx_info->packet_count = 1;

		if (pmac_tx->is_cck_rate)
			phydm_start_cck_cont_tx_jgr3(dm, tx_info);
		else
			phydm_start_ofdm_cont_tx_jgr3(dm);
	}
}

void phydm_set_pmac_txon_jgr3(void *dm_void, struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;

	odm_set_bb_reg(dm, R_0x1d08, BIT(0), 1); /* Turn on PMAC */

	/* mac scramble seed setting, only in 8198F */
	#if (RTL8198F_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8198F)
			if (!odm_get_bb_reg(dm, R_0x1d10, BIT(16)))
				odm_set_bb_reg(dm, R_0x1d10, BIT(16), 1);
	#endif

	if (pmac_tx->is_cck_rate) {
		odm_set_bb_reg(dm, R_0x1e70, 0xf, 8); /* TX CCK ON */
		odm_set_bb_reg(dm, R_0x1a84, BIT(31), 0);
	} else {
		odm_set_bb_reg(dm, R_0x1e70, 0xf, 4); /* TX Ofdm ON */
	}

	if (tx_info->mode == OFDM_SINGLE_TONE_TX)
		phydm_set_single_tone_jgr3(dm, true, true, pmac_tx->path);
}

void phydm_set_pmac_tx_jgr3(void *dm_void, struct phydm_pmac_info *tx_info,
			    enum rf_path mpt_rf_path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;

	pmac_tx->is_cck_rate = phydm_is_cck_rate(dm, tx_info->tx_rate);
	pmac_tx->is_ofdm_rate = phydm_is_ofdm_rate(dm, tx_info->tx_rate);
	pmac_tx->is_ht_rate = phydm_is_ht_rate(dm, tx_info->tx_rate);
	pmac_tx->is_vht_rate = phydm_is_vht_rate(dm, tx_info->tx_rate);
	pmac_tx->path = mpt_rf_path;

	if (!tx_info->en_pmac_tx) {
		phydm_stop_pmac_tx_jgr3(dm, tx_info);
		return;
	}

	phydm_set_mode_jgr3(dm, tx_info, tx_info->mode);

	if (pmac_tx->is_cck_rate)
		phydm_set_cck_preamble_hdr_jgr3(dm, tx_info);
	else
		phydm_set_sig_jgr3(dm, tx_info);

	phydm_set_mac_phy_txinfo_jgr3(dm, tx_info);
	phydm_set_pmac_txon_jgr3(dm, tx_info);
}

void phydm_set_tmac_tx_jgr3(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	/* Turn on TMAC */
	if (odm_get_bb_reg(dm, R_0x1d08, BIT(0)))
		odm_set_bb_reg(dm, R_0x1d08, BIT(0), 0);

	/* mac scramble seed setting, only in 8198F */
	#if (RTL8198F_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8198F)
			if (odm_get_bb_reg(dm, R_0x1d10, BIT(16)))
				odm_set_bb_reg(dm, R_0x1d10, BIT(16), 0);
	#endif

	/* Turn on TMAC CCK */
	if ((odm_get_bb_reg(dm, R_0x1a84, BIT(31))) == 0)
		odm_set_bb_reg(dm, R_0x1a84, BIT(31), 1);
}
#endif

void phydm_start_cck_cont_tx(void *dm_void, struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_start_cck_cont_tx_jgr3(dm, tx_info);
}

void phydm_stop_cck_cont_tx(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_stop_cck_cont_tx_jgr3(dm);
}

void phydm_start_ofdm_cont_tx(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_start_ofdm_cont_tx_jgr3(dm);
}

void phydm_stop_ofdm_cont_tx(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_stop_ofdm_cont_tx_jgr3(dm);
}

void phydm_set_single_tone(void *dm_void, boolean is_single_tone,
			   boolean en_pmac_tx, u8 path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_set_single_tone_jgr3(dm, is_single_tone,
					   en_pmac_tx, path);
}

void phydm_set_pmac_tx(void *dm_void, struct phydm_pmac_info *tx_info,
		       enum rf_path mpt_rf_path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_set_pmac_tx_jgr3(dm, tx_info, mpt_rf_path);
}

void phydm_set_tmac_tx(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_set_tmac_tx_jgr3(dm);
}

#endif
