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

#ifndef _HALMAC_BB_RF_88XX_H_
#define _HALMAC_BB_RF_88XX_H_

#include "../halmac_api.h"

#if HALMAC_88XX_SUPPORT

enum halmac_ret_status
start_iqk_88xx(struct halmac_adapter *adapter, struct halmac_iqk_para *param);

enum halmac_ret_status
ctrl_pwr_tracking_88xx(struct halmac_adapter *adapter,
		       struct halmac_pwr_tracking_option *opt);

enum halmac_ret_status
get_iqk_status_88xx(struct halmac_adapter *adapter,
		    enum halmac_cmd_process_status *proc_status);

enum halmac_ret_status
get_pwr_trk_status_88xx(struct halmac_adapter *adapter,
			enum halmac_cmd_process_status *proc_status);

enum halmac_ret_status
get_psd_status_88xx(struct halmac_adapter *adapter,
		    enum halmac_cmd_process_status *proc_status, u8 *data,
		    u32 *size);

enum halmac_ret_status
psd_88xx(struct halmac_adapter *adapter, u16 start_psd, u16 end_psd);

enum halmac_ret_status
get_h2c_ack_iqk_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

enum halmac_ret_status
get_h2c_ack_pwr_trk_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

enum halmac_ret_status
get_psd_data_88xx(struct halmac_adapter *adapter, u8 *buf, u32 size);

#endif /* HALMAC_88XX_SUPPORT */

#endif/* _HALMAC_BB_RF_88XX_H_ */
