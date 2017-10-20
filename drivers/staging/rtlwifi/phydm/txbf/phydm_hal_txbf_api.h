/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
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
#ifndef __PHYDM_HAL_TXBF_API_H__
#define __PHYDM_HAL_TXBF_API_H__

#define tx_bf_nr(a, b) ((a > b) ? (b) : (a))

u8 beamforming_get_htndp_tx_rate(void *dm_void, u8 comp_steering_num_of_bfer);

u8 beamforming_get_vht_ndp_tx_rate(void *dm_void, u8 comp_steering_num_of_bfer);

u8 phydm_get_beamforming_sounding_info(void *dm_void, u16 *troughput,
				       u8 total_bfee_num, u8 *tx_rate);

u8 phydm_get_ndpa_rate(void *dm_void);

u8 phydm_get_mu_bfee_snding_decision(void *dm_void, u16 throughput);

#endif
