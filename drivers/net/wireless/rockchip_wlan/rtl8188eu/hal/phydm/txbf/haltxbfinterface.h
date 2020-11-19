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
#ifndef __HAL_TXBF_INTERFACE_H__
#define __HAL_TXBF_INTERFACE_H__

#ifdef PHYDM_BEAMFORMING_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#define a_SifsTime ((IS_WIRELESS_MODE_5G(adapter) || IS_WIRELESS_MODE_N_24G(adapter)) ? 16 : 10)

void beamforming_gid_paid(
	void *adapter,
	PRT_TCB tcb);

enum rt_status
beamforming_get_report_frame(
	void *adapter,
	PRT_RFD rfd,
	POCTET_STRING p_pdu_os);

void beamforming_get_ndpa_frame(
	void *dm_void,
	OCTET_STRING pdu_os);

boolean
send_fw_ht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	enum channel_width BW);

boolean
send_fw_vht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	u16 AID,
	enum channel_width BW);

boolean
send_sw_vht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	u16 AID,
	enum channel_width BW);

boolean
send_sw_ht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	enum channel_width BW);

#if (SUPPORT_MU_BF == 1)
enum rt_status
beamforming_get_vht_gid_mgnt_frame(
	void *adapter,
	PRT_RFD rfd,
	POCTET_STRING p_pdu_os);

boolean
send_sw_vht_gid_mgnt_frame(
	void *dm_void,
	u8 *RA,
	u8 idx);

boolean
send_sw_vht_bf_report_poll(
	void *dm_void,
	u8 *RA,
	boolean is_final_poll);

boolean
send_sw_vht_mu_ndpa_packet(
	void *dm_void,
	enum channel_width BW);
#else
#define beamforming_get_vht_gid_mgnt_frame(adapter, rfd, p_pdu_os) RT_STATUS_FAILURE
#define send_sw_vht_gid_mgnt_frame(dm_void, RA)
#define send_sw_vht_bf_report_poll(dm_void, RA, is_final_poll)
#define send_sw_vht_mu_ndpa_packet(dm_void, BW)
#endif

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

u32 beamforming_get_report_frame(
	void *dm_void,
	union recv_frame *precv_frame);

boolean
send_fw_ht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	enum channel_width BW);

boolean
send_sw_ht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	enum channel_width BW);

boolean
send_fw_vht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	u16 AID,
	enum channel_width BW);

boolean
send_sw_vht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	u16 AID,
	enum channel_width BW);
#endif

void beamforming_get_ndpa_frame(
	void *dm_void,
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	OCTET_STRING pdu_os
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	union recv_frame *precv_frame
#endif
	);

boolean
dbg_send_sw_vht_mundpa_packet(
	void *dm_void,
	enum channel_width BW);

#else
#define beamforming_get_ndpa_frame(dm, _pdu_os)
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
#define beamforming_get_report_frame(adapter, precv_frame) RT_STATUS_FAILURE
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#define beamforming_get_report_frame(adapter, rfd, p_pdu_os) RT_STATUS_FAILURE
#define beamforming_get_vht_gid_mgnt_frame(adapter, rfd, p_pdu_os) RT_STATUS_FAILURE
#endif
#define send_fw_ht_ndpa_packet(dm_void, RA, BW)
#define send_sw_ht_ndpa_packet(dm_void, RA, BW)
#define send_fw_vht_ndpa_packet(dm_void, RA, AID, BW)
#define send_sw_vht_ndpa_packet(dm_void, RA, AID, BW)
#define send_sw_vht_gid_mgnt_frame(dm_void, RA, idx)
#define send_sw_vht_bf_report_poll(dm_void, RA, is_final_poll)
#define send_sw_vht_mu_ndpa_packet(dm_void, BW)
#endif

#endif
