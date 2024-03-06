/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2020-2021, 2023 Intel Corporation
 */
#ifndef __iwl_fw_api_rfi_h__
#define __iwl_fw_api_rfi_h__

#define IWL_RFI_LUT_ENTRY_CHANNELS_NUM 15
#define IWL_RFI_LUT_SIZE 24
#define IWL_RFI_LUT_INSTALLED_SIZE 4

/**
 * struct iwl_rfi_lut_entry - an entry in the RFI frequency LUT.
 *
 * @freq: frequency
 * @channels: channels that can be interfered at frequency freq (at most 15)
 * @bands: the corresponding bands
 */
struct iwl_rfi_lut_entry {
	__le16 freq;
	u8 channels[IWL_RFI_LUT_ENTRY_CHANNELS_NUM];
	u8 bands[IWL_RFI_LUT_ENTRY_CHANNELS_NUM];
} __packed;

/**
 * struct iwl_rfi_config_cmd - RFI configuration table
 *
 * @table: a table can have 24 frequency/channel mappings
 * @oem: specifies if this is the default table or set by OEM
 * @reserved: (reserved/padding)
 */
struct iwl_rfi_config_cmd {
	struct iwl_rfi_lut_entry table[IWL_RFI_LUT_SIZE];
	u8 oem;
	u8 reserved[3];
} __packed; /* RFI_CONFIG_CMD_API_S_VER_1 */

/**
 * enum iwl_rfi_freq_table_status - status of the frequency table query
 * @RFI_FREQ_TABLE_OK: can be used
 * @RFI_FREQ_TABLE_DVFS_NOT_READY: DVFS is not ready yet, should try later
 * @RFI_FREQ_TABLE_DISABLED: the feature is disabled in FW
 */
enum iwl_rfi_freq_table_status {
	RFI_FREQ_TABLE_OK,
	RFI_FREQ_TABLE_DVFS_NOT_READY,
	RFI_FREQ_TABLE_DISABLED,
};

/**
 * struct iwl_rfi_freq_table_resp_cmd - get the rfi freq table used by FW
 *
 * @table: table used by FW
 * @status: see &iwl_rfi_freq_table_status
 */
struct iwl_rfi_freq_table_resp_cmd {
	struct iwl_rfi_lut_entry table[IWL_RFI_LUT_INSTALLED_SIZE];
	__le32 status;
} __packed; /* RFI_CONFIG_CMD_API_S_VER_1 */

/**
 * struct iwl_rfi_deactivate_notif - notifcation that FW disaled RFIm
 *
 * @reason: used only for a log message
 */
struct iwl_rfi_deactivate_notif {
	__le32 reason;
} __packed; /* RFI_DEACTIVATE_NTF_S_VER_1 */
#endif /* __iwl_fw_api_rfi_h__ */
