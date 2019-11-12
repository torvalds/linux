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

#ifndef _HALMAC_FW_88XX_H_
#define _HALMAC_FW_88XX_H_

#include "../halmac_api.h"

#if HALMAC_88XX_SUPPORT

#define HALMC_DDMA_POLLING_COUNT		1000

enum halmac_ret_status
download_firmware_88xx(struct halmac_adapter *adapter, u8 *fw_bin, u32 size);

enum halmac_ret_status
free_download_firmware_88xx(struct halmac_adapter *adapter,
			    enum halmac_dlfw_mem mem_sel, u8 *fw_bin, u32 size);

enum halmac_ret_status
reset_wifi_fw_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
get_fw_version_88xx(struct halmac_adapter *adapter,
		    struct halmac_fw_version *ver);

enum halmac_ret_status
check_fw_status_88xx(struct halmac_adapter *adapter, u8 *fw_status);

enum halmac_ret_status
dump_fw_dmem_88xx(struct halmac_adapter *adapter, u8 *dmem, u32 *size);

enum halmac_ret_status
cfg_max_dl_size_88xx(struct halmac_adapter *adapter, u32 size);

enum halmac_ret_status
enter_cpu_sleep_mode_88xx(struct halmac_adapter *adapter);

enum halmac_ret_status
get_cpu_mode_88xx(struct halmac_adapter *adapter,
		  enum halmac_wlcpu_mode *mode);

enum halmac_ret_status
send_general_info_88xx(struct halmac_adapter *adapter,
		       struct halmac_general_info *info);

enum halmac_ret_status
drv_fwctrl_88xx(struct halmac_adapter *adapter, u8 *payload, u32 size, u8 ack);

#endif /* HALMAC_88XX_SUPPORT */

#endif/* _HALMAC_FW_88XX_H_ */
