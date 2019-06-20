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
#ifndef __HAL_TXBF_8822B_H__
#define __HAL_TXBF_8822B_H__

#if (RTL8822B_SUPPORT == 1)
#ifdef PHYDM_BEAMFORMING_SUPPORT

void hal_txbf_8822b_enter(
	void *dm_void,
	u8 idx);

void hal_txbf_8822b_leave(
	void *dm_void,
	u8 idx);

void hal_txbf_8822b_status(
	void *dm_void,
	u8 beamform_idx);

void hal_txbf_8822b_config_gtab(
	void *dm_void);

void hal_txbf_8822b_fw_txbf(
	void *dm_void,
	u8 idx);
#else
#define hal_txbf_8822b_enter(dm_void, idx)
#define hal_txbf_8822b_leave(dm_void, idx)
#define hal_txbf_8822b_status(dm_void, idx)
#define hal_txbf_8822b_fw_txbf(dm_void, idx)
#define hal_txbf_8822b_config_gtab(dm_void)

#endif

#if (defined(CONFIG_BB_TXBF_API))
void phydm_8822btxbf_rfmode(void *dm_void, u8 su_bfee_cnt, u8 mu_bfee_cnt);

void phydm_8822b_sutxbfer_workaroud(void *dm_void, boolean enable_su_bfer,
				    u8 nc, u8 nr, u8 ng, u8 CB, u8 BW,
				    boolean is_vht);

#else
#define phydm_8822btxbf_rfmode(dm_void, su_bfee_cnt, mu_bfee_cnt)
#define phydm_8822b_sutxbfer_workaroud(dm_void, enable_su_bfer, nc, nr, ng, CB, BW, is_vht)
#endif

#else
#define hal_txbf_8822b_enter(dm_void, idx)
#define hal_txbf_8822b_leave(dm_void, idx)
#define hal_txbf_8822b_status(dm_void, idx)
#define hal_txbf_8822b_fw_txbf(dm_void, idx)
#define hal_txbf_8822b_config_gtab(dm_void)

#endif
#endif
