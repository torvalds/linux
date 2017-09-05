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
#ifndef __HAL_TXBF_INTERFACE_H__
#define __HAL_TXBF_INTERFACE_H__

#define beamforming_get_ndpa_frame(dm, _pdu_os)
#define beamforming_get_report_frame(adapter, precv_frame) RT_STATUS_FAILURE
#define send_fw_ht_ndpa_packet(dm_void, RA, BW)
#define send_sw_ht_ndpa_packet(dm_void, RA, BW)
#define send_fw_vht_ndpa_packet(dm_void, RA, AID, BW)
#define send_sw_vht_ndpa_packet(dm_void, RA, AID, BW)
#define send_sw_vht_gid_mgnt_frame(dm_void, RA, idx)
#define send_sw_vht_bf_report_poll(dm_void, RA, is_final_poll)
#define send_sw_vht_mu_ndpa_packet(dm_void, BW)

#endif
