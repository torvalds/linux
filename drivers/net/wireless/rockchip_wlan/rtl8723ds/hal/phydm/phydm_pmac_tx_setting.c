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
		odm_set_bb_reg(dm, R_0x1c3c, BIT(1), 0x1);

	/* Turn Off All Test mode */
	odm_set_bb_reg(dm, R_0x1ca4, 0x7, 0x0);

	odm_set_bb_reg(dm, R_0x1a00, 0x3000, rate);
	odm_set_bb_reg(dm, R_0x1a00, 0x3, 0x2); /* transmit mode */
	odm_set_bb_reg(dm, R_0x1a00, BIT(3), 0x1); /* turn on scrambler*/

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
	odm_set_bb_reg(dm, R_0x1a00, BIT(3), 0x1); /* turn on scrambler*/

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
		odm_set_bb_reg(dm, R_0x1c3c, BIT(0), 0x1);

	/* 2. set CCK test mode off, set to CCK normal mode */
	odm_set_bb_reg(dm, R_0x1a00, 0x3, 0x0);

	/* 3. turn on scramble setting */
	odm_set_bb_reg(dm, R_0x1a00, BIT(3), 0x1);

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

void phydm_stop_pmac_tx_jgr3(void *dm_void, struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;
	u32 tmp = 0;

	odm_set_bb_reg(dm, R_0x1e70, 0xf, 0x2); /* TX Stop */

	if (tx_info->mode == CONT_TX) {
		if (pmac_tx->is_cck_rate)
			phydm_stop_cck_cont_tx_jgr3(dm);
		else
			phydm_stop_ofdm_cont_tx_jgr3(dm);
	}
}

void phydm_set_mac_phy_txinfo_jgr3(void *dm_void,
				   struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;
	u32 tmp = 0;

	odm_set_bb_reg(dm, R_0xa58, 0x003f8000, tx_info->tx_rate);

	/*0x900[1] ndp_sound */
	odm_set_bb_reg(dm, R_0x900, BIT(1), tx_info->ndp_sound);

	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	tx_info->m_stbc = tx_info->m_stbc - 1;
	#endif
	/*0x900[27:24] txsc [29:28] bw [31:30] m_stbc */
	tmp = (tx_info->tx_sc) | ((tx_info->bw) << 4) |
		((tx_info->m_stbc) << 6);
	odm_set_bb_reg(dm, R_0x900, 0xff000000, tmp);

	if (tx_info->tx_sc == 1) /*upper*/
		odm_set_bb_reg(dm, R_0x1ae0, 0x7000, 0x5);
	else if (tx_info->tx_sc == 2) /*lower*/
		odm_set_bb_reg(dm, R_0x1ae0, 0x7000, 0x6);
	else /* duplicate*/
		odm_set_bb_reg(dm, R_0x1ae0, 0x7000, 0x0);

	if (pmac_tx->is_ofdm_rate) {
		odm_set_bb_reg(dm, R_0x900, BIT(0), 0x0);
		odm_set_bb_reg(dm, R_0x900, BIT(2), 0x0);
	} else if (pmac_tx->is_ht_rate) {
		odm_set_bb_reg(dm, R_0x900, BIT(0), 0x1);
		odm_set_bb_reg(dm, R_0x900, BIT(2), 0x0);
	} else if (pmac_tx->is_vht_rate) {
		odm_set_bb_reg(dm, R_0x900, BIT(0), 0x0);
		odm_set_bb_reg(dm, R_0x900, BIT(2), 0x1);
	}

	/* for TX interval */
	odm_set_bb_reg(dm, R_0x9b8, MASKHWORD, tx_info->packet_period);
}

void phydm_set_sig_jgr3(void *dm_void, struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;
	u32 tmp = 0;

	if (pmac_tx->is_cck_rate)
		return;

	odm_set_bb_reg(dm, R_0x1eb4, 0xfffff, tx_info->packet_count);

	/* L-SIG */
	tmp = BYTE_2_DWORD(0, tx_info->lsig[2], tx_info->lsig[1],
			   tx_info->lsig[0]);
	odm_set_bb_reg(dm, R_0x908, 0xffffff, tmp);
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
		odm_set_bb_reg(dm, R_0x914, 0x1fffffff, tmp);
		odm_set_bb_reg(dm, R_0x938, 0xff00, tx_info->vht_sig_b_crc);

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
	odm_set_bb_reg(dm, R_0x1e6c, MASKLWORD, tmp);

	if (tx_info->is_short_preamble)
		odm_set_bb_reg(dm, R_0x1e6c, BIT(16), 0x0);
	else
		odm_set_bb_reg(dm, R_0x1e6c, BIT(16), 0x1);
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
	}
}

void phydm_set_pmac_txon_jgr3(void *dm_void, struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_tx *pmac_tx = &dm->dm_pmac_tx_table;

	odm_set_bb_reg(dm, R_0x1d08, BIT(0), 0x1); /*Turn on PMAC */

	/*mac scramble seed setting, only in 8198F */
	#if (RTL8198F_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8198F)
			if (!odm_get_bb_reg(dm, R_0x1d10, BIT(16)))
				odm_set_bb_reg(dm, R_0x1d10, BIT(16), 0x1);
	#endif

	if (pmac_tx->is_cck_rate) {
		odm_set_bb_reg(dm, R_0x1e70, 0xf, 0x8); /*TX CCK ON */
		odm_set_bb_reg(dm, R_0x1a84, BIT(31), 0x0);
	} else {
		odm_set_bb_reg(dm, R_0x1e70, 0xf, 0x4); /*TX Ofdm ON */
	}
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
		odm_set_bb_reg(dm, R_0x1d08, BIT(0), 0x0);

	/* mac scramble seed setting, only in 8198F */
	#if (RTL8198F_SUPPORT)
		if (dm->support_ic_type & ODM_RTL8198F)
			if (odm_get_bb_reg(dm, R_0x1d10, BIT(16)))
				odm_set_bb_reg(dm, R_0x1d10, BIT(16), 0x0);
	#endif

	/* Turn on TMAC CCK */
	if (!odm_get_bb_reg(dm, R_0x1a84, BIT(31)))
		odm_set_bb_reg(dm, R_0x1a84, BIT(31), 0x1);
}
#endif

void phydm_start_cck_cont_tx(void *dm_void, struct phydm_pmac_info *tx_info)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_start_cck_cont_tx_jgr3(dm, tx_info);
	#endif
}

void phydm_stop_cck_cont_tx(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_stop_cck_cont_tx_jgr3(dm);
	#endif
}

void phydm_start_ofdm_cont_tx(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_start_ofdm_cont_tx_jgr3(dm);
	#endif
}

void phydm_stop_ofdm_cont_tx(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_stop_ofdm_cont_tx_jgr3(dm);
	#endif
}

void phydm_set_pmac_tx(void *dm_void, struct phydm_pmac_info *tx_info,
		       enum rf_path mpt_rf_path)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_set_pmac_tx_jgr3(dm, tx_info, mpt_rf_path);
	#endif
}

void phydm_set_tmac_tx(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		phydm_set_tmac_tx_jgr3(dm);
	#endif
}

void phydm_pmac_tx_dbg(void *dm_void, char input[][16], u32 *_used,
		       char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_pmac_info tx_info;
	char help[] = "-h";
	char dbg_buf[PHYDM_SNPRINT_SIZE] = {0};
	u32 var[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8 i = 0;
	u32 tx_cnt = 0x0;
	u8 poll_cnt = 0x0;

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var[0]);

	if (!(dm->support_ic_type & ODM_IC_JGR3_SERIES))
		return;

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[pmac_tx] basic : {1} {rate_idx}(only 1M & 6M) {count}\n");
	} else {
		for (i = 1; i < 7; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var[i]);
			}
		}

		tx_info.en_pmac_tx = true;
		tx_info.mode = PKTS_TX;
		tx_info.ndp_sound = false;
		tx_info.bw = CHANNEL_WIDTH_20;
		tx_info.tx_sc = 0x0; /*duplicate*/
		tx_info.m_stbc = 0x0; /*disable*/
		tx_info.packet_period = 2000; /*d'500 us*/
		tx_info.tx_rate = (u8)var[1];
		tx_info.packet_count = (u32)var[2];

		if (tx_info.tx_rate == ODM_RATE1M) {
			tx_info.signal_field = 0xa; /*rate = 1M*/
			tx_info.service_field = 0x0;
			tx_info.length = 8000; /*d'8000 us=1000 bytes*/
			tx_info.crc16[0] = 0x60;
			tx_info.crc16[1] = 0x8e;
			/*long preamble*/
			tx_info.is_short_preamble = false;
			tx_info.sfd = 0xf3a0;
		} else if (tx_info.tx_rate == ODM_RATE6M) {
			/*l-sig[3:0] = rate = 6M = 0xb*/
			/*l-sig[16:5] = length = 1000 bytes*/
			/*l-sig[17] = parity = 1*/
			tx_info.lsig[0] = 0xb;
			tx_info.lsig[1] = 0x7d;
			tx_info.lsig[2] = 0x2;
		}
		phydm_print_rate_2_buff(dm, tx_info.tx_rate, dbg_buf,
					PHYDM_SNPRINT_SIZE);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "rate=%s, count=%d, pkt_interval=500(us), length=1000(bytes)\n",
			 dbg_buf, tx_info.packet_count);

		if (phydm_stop_ic_trx(dm, PHYDM_SET) == PHYDM_SET_FAIL) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "check trx idle failed, please try again.\n");
			return;
		}

		phydm_reset_bb_hw_cnt(dm);
		phydm_set_pmac_tx_jgr3(dm, &tx_info, RF_PATH_A);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "pmac_tx enabled, please wait for tx_cnt = %d\n",
			 tx_info.packet_count);
		while (1) {
			if (phydm_is_cck_rate(dm, tx_info.tx_rate))
				tx_cnt = odm_get_bb_reg(dm, R_0x2de4,
							MASKLWORD);
			else
				tx_cnt = odm_get_bb_reg(dm, R_0x2de0,
							MASKLWORD);

			if (tx_cnt >= tx_info.packet_count || poll_cnt >= 10)
				break;

			ODM_delay_ms(100);
			poll_cnt++;
		}

		if (tx_cnt < tx_info.packet_count)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "polling time out(1s), tx_cnt = %d\n", tx_cnt);
		else
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "pmac_tx finished, poll_cnt = %d\n", poll_cnt);

		tx_info.en_pmac_tx = false;
		phydm_set_pmac_tx(dm, &tx_info, RF_PATH_A);
		phydm_set_tmac_tx(dm);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Stop pmac_tx and turn on true mac mode.\n");

		phydm_stop_ic_trx(dm, PHYDM_REVERT);
	}
	*_used = used;
	*_out_len = out_len;
}
#endif