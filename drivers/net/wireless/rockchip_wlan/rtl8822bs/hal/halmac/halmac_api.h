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

#ifndef _HALMAC_API_H_
#define _HALMAC_API_H_

#define HALMAC_SVN_VER  "11692M"

#define HALMAC_MAJOR_VER        1
#define HALMAC_PROTOTYPE_VER    6
#define HALMAC_MINOR_VER        5
#define HALMAC_PATCH_VER        "6*"

#define HALMAC_88XX_SUPPORT	(HALMAC_8821C_SUPPORT || \
				 HALMAC_8822B_SUPPORT || \
				 HALMAC_8822C_SUPPORT || \
				 HALMAC_8812F_SUPPORT)

#define HALMAC_88XX_V1_SUPPORT	HALMAC_8814B_SUPPORT

#include "halmac_2_platform.h"
#include "halmac_type.h"
#include "halmac_hw_cfg.h"
#include "halmac_usb_reg.h"
#include "halmac_sdio_reg.h"
#include "halmac_pcie_reg.h"
#include "halmac_bit2.h"
#include "halmac_reg2.h"

#if HALMAC_PLATFORM_TESTPROGRAM
#include "halmac_type_testprogram.h"
#endif

#ifndef HALMAC_USE_TYPEDEF
#define HALMAC_USE_TYPEDEF	1
#endif

#if HALMAC_USE_TYPEDEF
#include "halmac_typedef.h"
#endif

#if HALMAC_8822B_SUPPORT
#include "halmac_reg_8822b.h"
#include "halmac_bit_8822b.h"
#endif

#if HALMAC_8821C_SUPPORT
#include "halmac_reg_8821c.h"
#include "halmac_bit_8821c.h"
#endif

#if HALMAC_8814B_SUPPORT
#include "halmac_reg_8814b.h"
#include "halmac_bit_8814b.h"
#endif

#if HALMAC_8822C_SUPPORT
#include "halmac_reg_8822c.h"
#include "halmac_bit_8822c.h"
#endif

#if HALMAC_8812F_SUPPORT
#include "halmac_reg_8812f.h"
#include "halmac_bit_8812f.h"
#endif

#if (HALMAC_PLATFORM_WINDOWS || HALMAC_PLATFORM_LINUX)
#include "halmac_tx_desc_nic.h"
#include "halmac_tx_desc_buffer_nic.h"
#include "halmac_tx_desc_ie_nic.h"
#include "halmac_rx_desc_nic.h"
#include "halmac_tx_bd_nic.h"
#include "halmac_rx_bd_nic.h"
#include "halmac_fw_offload_c2h_nic.h"
#include "halmac_fw_offload_h2c_nic.h"
#include "halmac_h2c_extra_info_nic.h"
#include "halmac_original_c2h_nic.h"
#include "halmac_original_h2c_nic.h"
#endif

#if (HALMAC_PLATFORM_AP)
#include "halmac_rx_desc_ap.h"
#include "halmac_tx_desc_ap.h"
#include "halmac_tx_desc_buffer_ap.h"
#include "halmac_tx_desc_ie_ap.h"
#include "halmac_fw_offload_c2h_ap.h"
#include "halmac_fw_offload_h2c_ap.h"
#include "halmac_h2c_extra_info_ap.h"
#include "halmac_original_c2h_ap.h"
#include "halmac_original_h2c_ap.h"
#endif

#if HALMAC_DBG_MONITOR_IO
#include "halmac_dbg.h"
#endif
#include "halmac_tx_desc_chip.h"
#include "halmac_rx_desc_chip.h"
#include "halmac_tx_desc_buffer_chip.h"
#include "halmac_tx_desc_ie_chip.h"

enum halmac_ret_status
halmac_init_adapter(void *drv_adapter, struct halmac_platform_api *pltfm_api,
		    enum halmac_interface intf,
		    struct halmac_adapter **halmac_adapter,
		    struct halmac_api **halmac_api);

enum halmac_ret_status
halmac_deinit_adapter(struct halmac_adapter *adapter);

enum halmac_ret_status
halmac_halt_api(struct halmac_adapter *adapter);

enum halmac_ret_status
halmac_get_version(struct halmac_ver *version);

#endif
