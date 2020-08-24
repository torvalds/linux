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
#ifndef __HAL_TXBF_8814A_H__
#define __HAL_TXBF_8814A_H__

#if (RTL8814A_SUPPORT == 1)
#ifdef PHYDM_BEAMFORMING_SUPPORT

boolean
phydm_beamforming_set_iqgen_8814A(void *dm_void);

void hal_txbf_8814a_set_ndpa_rate(void *dm_void, u8 BW, u8 rate);

u8 hal_txbf_8814a_get_ntx(void *dm_void);

void hal_txbf_8814a_enter(void *dm_void, u8 idx);

void hal_txbf_8814a_leave(void *dm_void, u8 idx);

void hal_txbf_8814a_status(void *dm_void, u8 idx);

void hal_txbf_8814a_reset_tx_path(void *dm_void, u8 idx);

void hal_txbf_8814a_get_tx_rate(void *dm_void);

void hal_txbf_8814a_fw_txbf(void *dm_void, u8 idx);

#else

#define hal_txbf_8814a_set_ndpa_rate(dm_void, BW, rate)
#define hal_txbf_8814a_get_ntx(dm_void) 0
#define hal_txbf_8814a_enter(dm_void, idx)
#define hal_txbf_8814a_leave(dm_void, idx)
#define hal_txbf_8814a_status(dm_void, idx)
#define hal_txbf_8814a_reset_tx_path(dm_void, idx)
#define hal_txbf_8814a_get_tx_rate(dm_void)
#define hal_txbf_8814a_fw_txbf(dm_void, idx)
#define phydm_beamforming_set_iqgen_8814A(dm_void) 0

#endif

#else

#define hal_txbf_8814a_set_ndpa_rate(dm_void, BW, rate)
#define hal_txbf_8814a_get_ntx(dm_void) 0
#define hal_txbf_8814a_enter(dm_void, idx)
#define hal_txbf_8814a_leave(dm_void, idx)
#define hal_txbf_8814a_status(dm_void, idx)
#define hal_txbf_8814a_reset_tx_path(dm_void, idx)
#define hal_txbf_8814a_get_tx_rate(dm_void)
#define hal_txbf_8814a_fw_txbf(dm_void, idx)
#define phydm_beamforming_set_iqgen_8814A(dm_void) 0
#endif

#endif
