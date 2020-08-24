/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
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
 ******************************************************************************/

#ifndef _HALMAC_MIMO_88XX_H_
#define _HALMAC_MIMO_88XX_H_

#include "../halmac_api.h"

#if HALMAC_88XX_SUPPORT

enum halmac_ret_status
cfg_txbf_88xx(struct halmac_adapter *adapter, u8 userid, enum halmac_bw bw,
	      u8 txbf_en);

enum halmac_ret_status
cfg_mumimo_88xx(struct halmac_adapter *adapter,
		struct halmac_cfg_mumimo_para *param);

enum halmac_ret_status
cfg_sounding_88xx(struct halmac_adapter *adapter, enum halmac_snd_role role,
		  enum halmac_data_rate rate);

enum halmac_ret_status
del_sounding_88xx(struct halmac_adapter *adapter, enum halmac_snd_role role);

enum halmac_ret_status
su_bfee_entry_init_88xx(struct halmac_adapter *adapter, u8 userid, u16 paid);

enum halmac_ret_status
su_bfer_entry_init_88xx(struct halmac_adapter *adapter,
			struct halmac_su_bfer_init_para *param);

enum halmac_ret_status
mu_bfee_entry_init_88xx(struct halmac_adapter *adapter,
			struct halmac_mu_bfee_init_para *param);

enum halmac_ret_status
mu_bfer_entry_init_88xx(struct halmac_adapter *adapter,
			struct halmac_mu_bfer_init_para *param);

enum halmac_ret_status
su_bfee_entry_del_88xx(struct halmac_adapter *adapter, u8 userid);

enum halmac_ret_status
su_bfer_entry_del_88xx(struct halmac_adapter *adapter, u8 userid);

enum halmac_ret_status
mu_bfee_entry_del_88xx(struct halmac_adapter *adapter, u8 userid);

enum halmac_ret_status
mu_bfer_entry_del_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
cfg_csi_rate_88xx(struct halmac_adapter *adapter, u8 rssi, u8 cur_rate,
		  u8 fixrate_en, u8 *new_rate, u8 *bmp_ofdm54);

enum halmac_ret_status
fw_snding_88xx(struct halmac_adapter *adapter,
	       struct halmac_su_snding_info *su_info,
	       struct halmac_mu_snding_info *mu_info, u8 period);

enum halmac_ret_status
get_h2c_ack_fw_snding_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

enum halmac_ret_status
get_fw_snding_status_88xx(struct halmac_adapter *adapter,
			  enum halmac_cmd_process_status *proc_status);

#endif /* HALMAC_88XX_SUPPORT */

#endif/* _HALMAC_MIMO_88XX_H_ */
