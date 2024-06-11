/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2018-2024 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_nvm_reg_h__
#define __iwl_fw_api_nvm_reg_h__

/**
 * enum iwl_regulatory_and_nvm_subcmd_ids - regulatory/NVM commands
 */
enum iwl_regulatory_and_nvm_subcmd_ids {
	/**
	 * @NVM_ACCESS_COMPLETE: &struct iwl_nvm_access_complete_cmd
	 */
	NVM_ACCESS_COMPLETE = 0x0,

	/**
	 * @LARI_CONFIG_CHANGE: &struct iwl_lari_config_change_cmd_v1,
	 *	&struct iwl_lari_config_change_cmd_v2,
	 *	&struct iwl_lari_config_change_cmd_v3,
	 *	&struct iwl_lari_config_change_cmd_v4,
	 *	&struct iwl_lari_config_change_cmd_v5,
	 *	&struct iwl_lari_config_change_cmd_v6,
	 *	&struct iwl_lari_config_change_cmd_v7,
	 *	&struct iwl_lari_config_change_cmd_v10 or
	 *	&struct iwl_lari_config_change_cmd
	 */
	LARI_CONFIG_CHANGE = 0x1,

	/**
	 * @NVM_GET_INFO:
	 * Command is &struct iwl_nvm_get_info,
	 * response is &struct iwl_nvm_get_info_rsp
	 */
	NVM_GET_INFO = 0x2,

	/**
	 * @TAS_CONFIG: &union iwl_tas_config_cmd
	 */
	TAS_CONFIG = 0x3,

	/**
	 * @SAR_OFFSET_MAPPING_TABLE_CMD: &struct iwl_sar_offset_mapping_cmd
	 */
	SAR_OFFSET_MAPPING_TABLE_CMD = 0x4,

	/**
	 * @MCC_ALLOWED_AP_TYPE_CMD: &struct iwl_mcc_allowed_ap_type_cmd
	 */
	MCC_ALLOWED_AP_TYPE_CMD = 0x5,

	/**
	 * @PNVM_INIT_COMPLETE_NTFY: &struct iwl_pnvm_init_complete_ntfy
	 */
	PNVM_INIT_COMPLETE_NTFY = 0xFE,
};

/**
 * enum iwl_nvm_access_op - NVM access opcode
 * @IWL_NVM_READ: read NVM
 * @IWL_NVM_WRITE: write NVM
 */
enum iwl_nvm_access_op {
	IWL_NVM_READ	= 0,
	IWL_NVM_WRITE	= 1,
};

/**
 * enum iwl_nvm_access_target - target of the NVM_ACCESS_CMD
 * @NVM_ACCESS_TARGET_CACHE: access the cache
 * @NVM_ACCESS_TARGET_OTP: access the OTP
 * @NVM_ACCESS_TARGET_EEPROM: access the EEPROM
 */
enum iwl_nvm_access_target {
	NVM_ACCESS_TARGET_CACHE = 0,
	NVM_ACCESS_TARGET_OTP = 1,
	NVM_ACCESS_TARGET_EEPROM = 2,
};

/**
 * enum iwl_nvm_section_type - section types for NVM_ACCESS_CMD
 * @NVM_SECTION_TYPE_SW: software section
 * @NVM_SECTION_TYPE_REGULATORY: regulatory section
 * @NVM_SECTION_TYPE_CALIBRATION: calibration section
 * @NVM_SECTION_TYPE_PRODUCTION: production section
 * @NVM_SECTION_TYPE_REGULATORY_SDP: regulatory section used by 3168 series
 * @NVM_SECTION_TYPE_MAC_OVERRIDE: MAC override section
 * @NVM_SECTION_TYPE_PHY_SKU: PHY SKU section
 * @NVM_MAX_NUM_SECTIONS: number of sections
 */
enum iwl_nvm_section_type {
	NVM_SECTION_TYPE_SW = 1,
	NVM_SECTION_TYPE_REGULATORY = 3,
	NVM_SECTION_TYPE_CALIBRATION = 4,
	NVM_SECTION_TYPE_PRODUCTION = 5,
	NVM_SECTION_TYPE_REGULATORY_SDP = 8,
	NVM_SECTION_TYPE_MAC_OVERRIDE = 11,
	NVM_SECTION_TYPE_PHY_SKU = 12,
	NVM_MAX_NUM_SECTIONS = 13,
};

/**
 * struct iwl_nvm_access_cmd - Request the device to send an NVM section
 * @op_code: &enum iwl_nvm_access_op
 * @target: &enum iwl_nvm_access_target
 * @type: &enum iwl_nvm_section_type
 * @offset: offset in bytes into the section
 * @length: in bytes, to read/write
 * @data: if write operation, the data to write. On read its empty
 */
struct iwl_nvm_access_cmd {
	u8 op_code;
	u8 target;
	__le16 type;
	__le16 offset;
	__le16 length;
	u8 data[];
} __packed; /* NVM_ACCESS_CMD_API_S_VER_2 */

/**
 * struct iwl_nvm_access_resp_ver2 - response to NVM_ACCESS_CMD
 * @offset: offset in bytes into the section
 * @length: in bytes, either how much was written or read
 * @type: NVM_SECTION_TYPE_*
 * @status: 0 for success, fail otherwise
 * @data: if read operation, the data returned. Empty on write.
 */
struct iwl_nvm_access_resp {
	__le16 offset;
	__le16 length;
	__le16 type;
	__le16 status;
	u8 data[];
} __packed; /* NVM_ACCESS_CMD_RESP_API_S_VER_2 */

/*
 * struct iwl_nvm_get_info - request to get NVM data
 */
struct iwl_nvm_get_info {
	__le32 reserved;
} __packed; /* REGULATORY_NVM_GET_INFO_CMD_API_S_VER_1 */

/**
 * enum iwl_nvm_info_general_flags - flags in NVM_GET_INFO resp
 * @NVM_GENERAL_FLAGS_EMPTY_OTP: 1 if OTP is empty
 */
enum iwl_nvm_info_general_flags {
	NVM_GENERAL_FLAGS_EMPTY_OTP	= BIT(0),
};

/**
 * struct iwl_nvm_get_info_general - general NVM data
 * @flags: bit 0: 1 - empty, 0 - non-empty
 * @nvm_version: nvm version
 * @board_type: board type
 * @n_hw_addrs: number of reserved MAC addresses
 */
struct iwl_nvm_get_info_general {
	__le32 flags;
	__le16 nvm_version;
	u8 board_type;
	u8 n_hw_addrs;
} __packed; /* REGULATORY_NVM_GET_INFO_GENERAL_S_VER_2 */

/**
 * enum iwl_nvm_mac_sku_flags - flags in &iwl_nvm_get_info_sku
 * @NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED: true if 2.4 band enabled
 * @NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED: true if 5.2 band enabled
 * @NVM_MAC_SKU_FLAGS_802_11N_ENABLED: true if 11n enabled
 * @NVM_MAC_SKU_FLAGS_802_11AC_ENABLED: true if 11ac enabled
 * @NVM_MAC_SKU_FLAGS_MIMO_DISABLED: true if MIMO disabled
 * @NVM_MAC_SKU_FLAGS_WAPI_ENABLED: true if WAPI enabled
 * @NVM_MAC_SKU_FLAGS_REG_CHECK_ENABLED: true if regulatory checker enabled
 * @NVM_MAC_SKU_FLAGS_API_LOCK_ENABLED: true if API lock enabled
 */
enum iwl_nvm_mac_sku_flags {
	NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED	= BIT(0),
	NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED	= BIT(1),
	NVM_MAC_SKU_FLAGS_802_11N_ENABLED	= BIT(2),
	NVM_MAC_SKU_FLAGS_802_11AC_ENABLED	= BIT(3),
	/**
	 * @NVM_MAC_SKU_FLAGS_802_11AX_ENABLED: true if 11ax enabled
	 */
	NVM_MAC_SKU_FLAGS_802_11AX_ENABLED	= BIT(4),
	NVM_MAC_SKU_FLAGS_MIMO_DISABLED		= BIT(5),
	NVM_MAC_SKU_FLAGS_WAPI_ENABLED		= BIT(8),
	NVM_MAC_SKU_FLAGS_REG_CHECK_ENABLED	= BIT(14),
	NVM_MAC_SKU_FLAGS_API_LOCK_ENABLED	= BIT(15),
};

/**
 * struct iwl_nvm_get_info_sku - mac information
 * @mac_sku_flags: flags for SKU, see &enum iwl_nvm_mac_sku_flags
 */
struct iwl_nvm_get_info_sku {
	__le32 mac_sku_flags;
} __packed; /* REGULATORY_NVM_GET_INFO_MAC_SKU_SECTION_S_VER_2 */

/**
 * struct iwl_nvm_get_info_phy - phy information
 * @tx_chains: BIT 0 chain A, BIT 1 chain B
 * @rx_chains: BIT 0 chain A, BIT 1 chain B
 */
struct iwl_nvm_get_info_phy {
	__le32 tx_chains;
	__le32 rx_chains;
} __packed; /* REGULATORY_NVM_GET_INFO_PHY_SKU_SECTION_S_VER_1 */

#define IWL_NUM_CHANNELS_V1	51
#define IWL_NUM_CHANNELS	110

/**
 * struct iwl_nvm_get_info_regulatory - regulatory information
 * @lar_enabled: is LAR enabled
 * @channel_profile: regulatory data of this channel
 * @reserved: reserved
 */
struct iwl_nvm_get_info_regulatory_v1 {
	__le32 lar_enabled;
	__le16 channel_profile[IWL_NUM_CHANNELS_V1];
	__le16 reserved;
} __packed; /* REGULATORY_NVM_GET_INFO_REGULATORY_S_VER_1 */

/**
 * struct iwl_nvm_get_info_regulatory - regulatory information
 * @lar_enabled: is LAR enabled
 * @n_channels: number of valid channels in the array
 * @channel_profile: regulatory data of this channel
 */
struct iwl_nvm_get_info_regulatory {
	__le32 lar_enabled;
	__le32 n_channels;
	__le32 channel_profile[IWL_NUM_CHANNELS];
} __packed; /* REGULATORY_NVM_GET_INFO_REGULATORY_S_VER_2 */

/**
 * struct iwl_nvm_get_info_rsp_v3 - response to get NVM data
 * @general: general NVM data
 * @mac_sku: data relating to MAC sku
 * @phy_sku: data relating to PHY sku
 * @regulatory: regulatory data
 */
struct iwl_nvm_get_info_rsp_v3 {
	struct iwl_nvm_get_info_general general;
	struct iwl_nvm_get_info_sku mac_sku;
	struct iwl_nvm_get_info_phy phy_sku;
	struct iwl_nvm_get_info_regulatory_v1 regulatory;
} __packed; /* REGULATORY_NVM_GET_INFO_RSP_API_S_VER_3 */

/**
 * struct iwl_nvm_get_info_rsp - response to get NVM data
 * @general: general NVM data
 * @mac_sku: data relating to MAC sku
 * @phy_sku: data relating to PHY sku
 * @regulatory: regulatory data
 */
struct iwl_nvm_get_info_rsp {
	struct iwl_nvm_get_info_general general;
	struct iwl_nvm_get_info_sku mac_sku;
	struct iwl_nvm_get_info_phy phy_sku;
	struct iwl_nvm_get_info_regulatory regulatory;
} __packed; /* REGULATORY_NVM_GET_INFO_RSP_API_S_VER_4 */

/**
 * struct iwl_nvm_access_complete_cmd - NVM_ACCESS commands are completed
 * @reserved: reserved
 */
struct iwl_nvm_access_complete_cmd {
	__le32 reserved;
} __packed; /* NVM_ACCESS_COMPLETE_CMD_API_S_VER_1 */

#define IWL_MCC_US	0x5553
#define IWL_MCC_CANADA	0x4341

/**
 * struct iwl_mcc_update_cmd - Request the device to update geographic
 * regulatory profile according to the given MCC (Mobile Country Code).
 * The MCC is two letter-code, ascii upper case[A-Z] or '00' for world domain.
 * 'ZZ' MCC will be used to switch to NVM default profile; in this case, the
 * MCC in the cmd response will be the relevant MCC in the NVM.
 * @mcc: given mobile country code
 * @source_id: the source from where we got the MCC, see iwl_mcc_source
 * @reserved: reserved for alignment
 * @key: integrity key for MCC API OEM testing
 * @reserved2: reserved
 */
struct iwl_mcc_update_cmd {
	__le16 mcc;
	u8 source_id;
	u8 reserved;
	__le32 key;
	u8 reserved2[20];
} __packed; /* LAR_UPDATE_MCC_CMD_API_S_VER_2 */

/**
 * enum iwl_geo_information - geographic information.
 * @GEO_NO_INFO: no special info for this geo profile.
 * @GEO_WMM_ETSI_5GHZ_INFO: this geo profile limits the WMM params
 *	for the 5 GHz band.
 */
enum iwl_geo_information {
	GEO_NO_INFO =			0,
	GEO_WMM_ETSI_5GHZ_INFO =	BIT(0),
};

/**
 * struct iwl_mcc_update_resp_v3 - response to MCC_UPDATE_CMD.
 * Contains the new channel control profile map, if changed, and the new MCC
 * (mobile country code).
 * The new MCC may be different than what was requested in MCC_UPDATE_CMD.
 * @status: see &enum iwl_mcc_update_status
 * @mcc: the new applied MCC
 * @cap: capabilities for all channels which matches the MCC
 * @source_id: the MCC source, see iwl_mcc_source
 * @time: time elapsed from the MCC test start (in units of 30 seconds)
 * @geo_info: geographic specific profile information
 *	see &enum iwl_geo_information.
 * @n_channels: number of channels in @channels_data.
 * @channels: channel control data map, DWORD for each channel. Only the first
 *	16bits are used.
 */
struct iwl_mcc_update_resp_v3 {
	__le32 status;
	__le16 mcc;
	u8 cap;
	u8 source_id;
	__le16 time;
	__le16 geo_info;
	__le32 n_channels;
	__le32 channels[];
} __packed; /* LAR_UPDATE_MCC_CMD_RESP_S_VER_3 */

/**
 * struct iwl_mcc_update_resp_v4 - response to MCC_UPDATE_CMD.
 * Contains the new channel control profile map, if changed, and the new MCC
 * (mobile country code).
 * The new MCC may be different than what was requested in MCC_UPDATE_CMD.
 * @status: see &enum iwl_mcc_update_status
 * @mcc: the new applied MCC
 * @cap: capabilities for all channels which matches the MCC
 * @time: time elapsed from the MCC test start (in units of 30 seconds)
 * @geo_info: geographic specific profile information
 *	see &enum iwl_geo_information.
 * @source_id: the MCC source, see iwl_mcc_source
 * @reserved: for four bytes alignment.
 * @n_channels: number of channels in @channels_data.
 * @channels: channel control data map, DWORD for each channel. Only the first
 *	16bits are used.
 */
struct iwl_mcc_update_resp_v4 {
	__le32 status;
	__le16 mcc;
	__le16 cap;
	__le16 time;
	__le16 geo_info;
	u8 source_id;
	u8 reserved[3];
	__le32 n_channels;
	__le32 channels[];
} __packed; /* LAR_UPDATE_MCC_CMD_RESP_S_VER_4 */

/**
 * struct iwl_mcc_update_resp_v8 - response to MCC_UPDATE_CMD.
 * Contains the new channel control profile map, if changed, and the new MCC
 * (mobile country code).
 * The new MCC may be different than what was requested in MCC_UPDATE_CMD.
 * @status: see &enum iwl_mcc_update_status
 * @mcc: the new applied MCC
 * @padding: padding for 2 bytes.
 * @cap: capabilities for all channels which matches the MCC
 * @time: time elapsed from the MCC test start (in units of 30 seconds)
 * @geo_info: geographic specific profile information
 *     see &enum iwl_geo_information.
 * @source_id: the MCC source, see iwl_mcc_source
 * @reserved: for four bytes alignment.
 * @n_channels: number of channels in @channels_data.
 * @channels: channel control data map, DWORD for each channel. Only the first
 *     16bits are used.
 */
struct iwl_mcc_update_resp_v8 {
	__le32 status;
	__le16 mcc;
	u8 padding[2];
	__le32 cap;
	__le16 time;
	__le16 geo_info;
	u8 source_id;
	u8 reserved[3];
	__le32 n_channels;
	__le32 channels[];
} __packed; /* LAR_UPDATE_MCC_CMD_RESP_S_VER_8 */

/**
 * struct iwl_mcc_chub_notif - chub notifies of mcc change
 * (MCC_CHUB_UPDATE_CMD = 0xc9)
 * The Chub (Communication Hub, CommsHUB) is a HW component that connects to
 * the cellular and connectivity cores that gets updates of the mcc, and
 * notifies the ucode directly of any mcc change.
 * The ucode requests the driver to request the device to update geographic
 * regulatory  profile according to the given MCC (Mobile Country Code).
 * The MCC is two letter-code, ascii upper case[A-Z] or '00' for world domain.
 * 'ZZ' MCC will be used to switch to NVM default profile; in this case, the
 * MCC in the cmd response will be the relevant MCC in the NVM.
 * @mcc: given mobile country code
 * @source_id: identity of the change originator, see iwl_mcc_source
 * @reserved1: reserved for alignment
 */
struct iwl_mcc_chub_notif {
	__le16 mcc;
	u8 source_id;
	u8 reserved1;
} __packed; /* LAR_MCC_NOTIFY_S */

enum iwl_mcc_update_status {
	MCC_RESP_NEW_CHAN_PROFILE,
	MCC_RESP_SAME_CHAN_PROFILE,
	MCC_RESP_INVALID,
	MCC_RESP_NVM_DISABLED,
	MCC_RESP_ILLEGAL,
	MCC_RESP_LOW_PRIORITY,
	MCC_RESP_TEST_MODE_ACTIVE,
	MCC_RESP_TEST_MODE_NOT_ACTIVE,
	MCC_RESP_TEST_MODE_DENIAL_OF_SERVICE,
};

enum iwl_mcc_source {
	MCC_SOURCE_OLD_FW = 0,
	MCC_SOURCE_ME = 1,
	MCC_SOURCE_BIOS = 2,
	MCC_SOURCE_3G_LTE_HOST = 3,
	MCC_SOURCE_3G_LTE_DEVICE = 4,
	MCC_SOURCE_WIFI = 5,
	MCC_SOURCE_RESERVED = 6,
	MCC_SOURCE_DEFAULT = 7,
	MCC_SOURCE_UNINITIALIZED = 8,
	MCC_SOURCE_MCC_API = 9,
	MCC_SOURCE_GET_CURRENT = 0x10,
	MCC_SOURCE_GETTING_MCC_TEST_MODE = 0x11,
};

#define IWL_WTAS_BLACK_LIST_MAX		16
/**
 * struct iwl_tas_config_cmd_common - configures the TAS.
 * This is also the v2 structure.
 * @block_list_size: size of relevant field in block_list_array
 * @block_list_array: list of countries where TAS must be disabled
 */
struct iwl_tas_config_cmd_common {
	__le32 block_list_size;
	__le32 block_list_array[IWL_WTAS_BLACK_LIST_MAX];
} __packed; /* TAS_CONFIG_CMD_API_S_VER_2 */

/**
 * struct iwl_tas_config_cmd_v3 - configures the TAS
 * @override_tas_iec: indicates whether to override default value of IEC regulatory
 * @enable_tas_iec: in case override_tas_iec is set -
 *	indicates whether IEC regulatory is enabled or disabled
 */
struct iwl_tas_config_cmd_v3 {
	__le16 override_tas_iec;
	__le16 enable_tas_iec;
} __packed; /* TAS_CONFIG_CMD_API_S_VER_3 */

/**
 * struct iwl_tas_config_cmd_v4 - configures the TAS
 * @override_tas_iec: indicates whether to override default value of IEC regulatory
 * @enable_tas_iec: in case override_tas_iec is set -
 *	indicates whether IEC regulatory is enabled or disabled
 * @usa_tas_uhb_allowed: if set, allow TAS UHB in the USA
 * @reserved: reserved
*/
struct iwl_tas_config_cmd_v4 {
	u8 override_tas_iec;
	u8 enable_tas_iec;
	u8 usa_tas_uhb_allowed;
	u8 reserved;
} __packed; /* TAS_CONFIG_CMD_API_S_VER_4 */

struct iwl_tas_config_cmd {
	struct iwl_tas_config_cmd_common common;
	union {
		struct iwl_tas_config_cmd_v3 v3;
		struct iwl_tas_config_cmd_v4 v4;
	};
};

/**
 * enum iwl_lari_config_masks - bit masks for the various LARI config operations
 * @LARI_CONFIG_DISABLE_11AC_UKRAINE_MSK: disable 11ac in ukraine
 * @LARI_CONFIG_CHANGE_ETSI_TO_PASSIVE_MSK: ETSI 5.8GHz SRD passive scan
 * @LARI_CONFIG_CHANGE_ETSI_TO_DISABLED_MSK: ETSI 5.8GHz SRD disabled
 * @LARI_CONFIG_ENABLE_5G2_IN_INDONESIA_MSK: enable 5.15/5.35GHz bands in
 * 	Indonesia
 * @LARI_CONFIG_ENABLE_CHINA_22_REG_SUPPORT_MSK: enable 2022 china regulatory
 */
enum iwl_lari_config_masks {
	LARI_CONFIG_DISABLE_11AC_UKRAINE_MSK		= BIT(0),
	LARI_CONFIG_CHANGE_ETSI_TO_PASSIVE_MSK		= BIT(1),
	LARI_CONFIG_CHANGE_ETSI_TO_DISABLED_MSK		= BIT(2),
	LARI_CONFIG_ENABLE_5G2_IN_INDONESIA_MSK		= BIT(3),
	LARI_CONFIG_ENABLE_CHINA_22_REG_SUPPORT_MSK	= BIT(7),
};

#define IWL_11AX_UKRAINE_MASK 3
#define IWL_11AX_UKRAINE_SHIFT 8

/**
 * struct iwl_lari_config_change_cmd_v1 - change LARI configuration
 * @config_bitmap: bit map of the config commands. each bit will trigger a
 * different predefined FW config operation
 */
struct iwl_lari_config_change_cmd_v1 {
	__le32 config_bitmap;
} __packed; /* LARI_CHANGE_CONF_CMD_S_VER_1 */

/**
 * struct iwl_lari_config_change_cmd_v2 - change LARI configuration
 * @config_bitmap: bit map of the config commands. each bit will trigger a
 * different predefined FW config operation
 * @oem_uhb_allow_bitmap: bitmap of UHB enabled MCC sets
 */
struct iwl_lari_config_change_cmd_v2 {
	__le32 config_bitmap;
	__le32 oem_uhb_allow_bitmap;
} __packed; /* LARI_CHANGE_CONF_CMD_S_VER_2 */

/**
 * struct iwl_lari_config_change_cmd_v3 - change LARI configuration
 * @config_bitmap: bit map of the config commands. each bit will trigger a
 * different predefined FW config operation
 * @oem_uhb_allow_bitmap: bitmap of UHB enabled MCC sets
 * @oem_11ax_allow_bitmap: bitmap of 11ax allowed MCCs.
 * For each supported country, a pair of regulatory override bit and 11ax mode exist
 * in the bit field.
 */
struct iwl_lari_config_change_cmd_v3 {
	__le32 config_bitmap;
	__le32 oem_uhb_allow_bitmap;
	__le32 oem_11ax_allow_bitmap;
} __packed; /* LARI_CHANGE_CONF_CMD_S_VER_3 */

/**
 * struct iwl_lari_config_change_cmd_v4 - change LARI configuration
 * @config_bitmap: Bitmap of the config commands. Each bit will trigger a
 *     different predefined FW config operation.
 * @oem_uhb_allow_bitmap: Bitmap of UHB enabled MCC sets.
 * @oem_11ax_allow_bitmap: Bitmap of 11ax allowed MCCs. There are two bits
 *     per country, one to indicate whether to override and the other to
 *     indicate the value to use.
 * @oem_unii4_allow_bitmap: Bitmap of unii4 allowed MCCs.There are two bits
 *     per country, one to indicate whether to override and the other to
 *     indicate allow/disallow unii4 channels.
 */
struct iwl_lari_config_change_cmd_v4 {
	__le32 config_bitmap;
	__le32 oem_uhb_allow_bitmap;
	__le32 oem_11ax_allow_bitmap;
	__le32 oem_unii4_allow_bitmap;
} __packed; /* LARI_CHANGE_CONF_CMD_S_VER_4 */

/**
 * struct iwl_lari_config_change_cmd_v5 - change LARI configuration
 * @config_bitmap: Bitmap of the config commands. Each bit will trigger a
 *     different predefined FW config operation.
 * @oem_uhb_allow_bitmap: Bitmap of UHB enabled MCC sets.
 * @oem_11ax_allow_bitmap: Bitmap of 11ax allowed MCCs. There are two bits
 *     per country, one to indicate whether to override and the other to
 *     indicate the value to use.
 * @oem_unii4_allow_bitmap: Bitmap of unii4 allowed MCCs.There are two bits
 *     per country, one to indicate whether to override and the other to
 *     indicate allow/disallow unii4 channels.
 * @chan_state_active_bitmap: Bitmap for overriding channel state to active.
 *     Each bit represents a country or region to activate, according to the BIOS
 *     definitions.
 */
struct iwl_lari_config_change_cmd_v5 {
	__le32 config_bitmap;
	__le32 oem_uhb_allow_bitmap;
	__le32 oem_11ax_allow_bitmap;
	__le32 oem_unii4_allow_bitmap;
	__le32 chan_state_active_bitmap;
} __packed; /* LARI_CHANGE_CONF_CMD_S_VER_5 */

/**
 * struct iwl_lari_config_change_cmd_v6 - change LARI configuration
 * @config_bitmap: Bitmap of the config commands. Each bit will trigger a
 *     different predefined FW config operation.
 * @oem_uhb_allow_bitmap: Bitmap of UHB enabled MCC sets.
 * @oem_11ax_allow_bitmap: Bitmap of 11ax allowed MCCs. There are two bits
 *     per country, one to indicate whether to override and the other to
 *     indicate the value to use.
 * @oem_unii4_allow_bitmap: Bitmap of unii4 allowed MCCs.There are two bits
 *     per country, one to indicate whether to override and the other to
 *     indicate allow/disallow unii4 channels.
 * @chan_state_active_bitmap: Bitmap for overriding channel state to active.
 *     Each bit represents a country or region to activate, according to the BIOS
 *     definitions.
 * @force_disable_channels_bitmap: Bitmap of disabled bands/channels.
 *     Each bit represents a set of channels in a specific band that should be disabled
 */
struct iwl_lari_config_change_cmd_v6 {
	__le32 config_bitmap;
	__le32 oem_uhb_allow_bitmap;
	__le32 oem_11ax_allow_bitmap;
	__le32 oem_unii4_allow_bitmap;
	__le32 chan_state_active_bitmap;
	__le32 force_disable_channels_bitmap;
} __packed; /* LARI_CHANGE_CONF_CMD_S_VER_6 */

/**
 * struct iwl_lari_config_change_cmd_v7 - change LARI configuration
 * This structure is used also for lari cmd version 8 and 9.
 * @config_bitmap: Bitmap of the config commands. Each bit will trigger a
 *     different predefined FW config operation.
 * @oem_uhb_allow_bitmap: Bitmap of UHB enabled MCC sets.
 * @oem_11ax_allow_bitmap: Bitmap of 11ax allowed MCCs. There are two bits
 *     per country, one to indicate whether to override and the other to
 *     indicate the value to use.
 * @oem_unii4_allow_bitmap: Bitmap of unii4 allowed MCCs.There are two bits
 *     per country, one to indicate whether to override and the other to
 *     indicate allow/disallow unii4 channels.
 *     For LARI cmd version 4 to 8 - bits 0:3 are supported.
 *     For LARI cmd version 9 - bits 0:5 are supported.
 * @chan_state_active_bitmap: Bitmap to enable different bands per country
 *     or region.
 *     Each bit represents a country or region, and a band to activate
 *     according to the BIOS definitions.
 *     For LARI cmd version 7 - bits 0:3 are supported.
 *     For LARI cmd version 8 - bits 0:4 are supported.
 * @force_disable_channels_bitmap: Bitmap of disabled bands/channels.
 *     Each bit represents a set of channels in a specific band that should be
 *     disabled
 * @edt_bitmap: Bitmap of energy detection threshold table.
 *	Disable/enable the EDT optimization method for different band.
 */
struct iwl_lari_config_change_cmd_v7 {
	__le32 config_bitmap;
	__le32 oem_uhb_allow_bitmap;
	__le32 oem_11ax_allow_bitmap;
	__le32 oem_unii4_allow_bitmap;
	__le32 chan_state_active_bitmap;
	__le32 force_disable_channels_bitmap;
	__le32 edt_bitmap;
} __packed;
/* LARI_CHANGE_CONF_CMD_S_VER_7 */
/* LARI_CHANGE_CONF_CMD_S_VER_8 */
/* LARI_CHANGE_CONF_CMD_S_VER_9 */

/**
 * struct iwl_lari_config_change_cmd_v10 - change LARI configuration
 * @config_bitmap: Bitmap of the config commands. Each bit will trigger a
 *	different predefined FW config operation.
 * @oem_uhb_allow_bitmap: Bitmap of UHB enabled MCC sets.
 * @oem_11ax_allow_bitmap: Bitmap of 11ax allowed MCCs. There are two bits
 *	per country, one to indicate whether to override and the other to
 *	indicate the value to use.
 * @oem_unii4_allow_bitmap: Bitmap of unii4 allowed MCCs.There are two bits
 *	per country, one to indicate whether to override and the other to
 *	indicate allow/disallow unii4 channels.
 *	For LARI cmd version 10 - bits 0:5 are supported.
 * @chan_state_active_bitmap: Bitmap to enable different bands per country
 *	or region.
 *	Each bit represents a country or region, and a band to activate
 *	according to the BIOS definitions.
 *	For LARI cmd version 10 - bits 0:4 are supported.
 * @force_disable_channels_bitmap: Bitmap of disabled bands/channels.
 *	Each bit represents a set of channels in a specific band that should be
 *	disabled
 * @edt_bitmap: Bitmap of energy detection threshold table.
 *	Disable/enable the EDT optimization method for different band.
 * @oem_320mhz_allow_bitmap: 320Mhz bandwidth enablement bitmap per MCC.
 *	bit0: enable 320Mhz in Japan.
 *	bit1: enable 320Mhz in South Korea.
 *	bit 2 - 31: reserved.
 */
struct iwl_lari_config_change_cmd_v10 {
	__le32 config_bitmap;
	__le32 oem_uhb_allow_bitmap;
	__le32 oem_11ax_allow_bitmap;
	__le32 oem_unii4_allow_bitmap;
	__le32 chan_state_active_bitmap;
	__le32 force_disable_channels_bitmap;
	__le32 edt_bitmap;
	__le32 oem_320mhz_allow_bitmap;
} __packed;
/* LARI_CHANGE_CONF_CMD_S_VER_10 */

/**
 * struct iwl_lari_config_change_cmd - change LARI configuration
 * @config_bitmap: Bitmap of the config commands. Each bit will trigger a
 *	different predefined FW config operation.
 * @oem_uhb_allow_bitmap: Bitmap of UHB enabled MCC sets.
 * @oem_11ax_allow_bitmap: Bitmap of 11ax allowed MCCs. There are two bits
 *	per country, one to indicate whether to override and the other to
 *	indicate the value to use.
 * @oem_unii4_allow_bitmap: Bitmap of unii4 allowed MCCs.There are two bits
 *	per country, one to indicate whether to override and the other to
 *	indicate allow/disallow unii4 channels.
 *	For LARI cmd version 11 - bits 0:5 are supported.
 * @chan_state_active_bitmap: Bitmap to enable different bands per country
 *	or region.
 *	Each bit represents a country or region, and a band to activate
 *	according to the BIOS definitions.
 *	For LARI cmd version 11 - bits 0:4 are supported.
 * @force_disable_channels_bitmap: Bitmap of disabled bands/channels.
 *	Each bit represents a set of channels in a specific band that should be
 *	disabled
 * @edt_bitmap: Bitmap of energy detection threshold table.
 *	Disable/enable the EDT optimization method for different band.
 * @oem_320mhz_allow_bitmap: 320Mhz bandwidth enablement bitmap per MCC.
 *	bit0: enable 320Mhz in Japan.
 *	bit1: enable 320Mhz in South Korea.
 *	bit 2 - 31: reserved.
 * @oem_11be_allow_bitmap: Bitmap of 11be allowed MCCs. No need to mask out the
 *	unsupported bits
 *	bit0: enable 11be in China(CB/CN).
 *	bit1: enable 11be in South Korea.
 *	bit 2 - 31: reserved.
 */
struct iwl_lari_config_change_cmd {
	__le32 config_bitmap;
	__le32 oem_uhb_allow_bitmap;
	__le32 oem_11ax_allow_bitmap;
	__le32 oem_unii4_allow_bitmap;
	__le32 chan_state_active_bitmap;
	__le32 force_disable_channels_bitmap;
	__le32 edt_bitmap;
	__le32 oem_320mhz_allow_bitmap;
	__le32 oem_11be_allow_bitmap;
} __packed;
/* LARI_CHANGE_CONF_CMD_S_VER_11 */

/* Activate UNII-1 (5.2GHz) for World Wide */
#define ACTIVATE_5G2_IN_WW_MASK	BIT(4)

/**
 * struct iwl_pnvm_init_complete_ntfy - PNVM initialization complete
 * @status: PNVM image loading status
 */
struct iwl_pnvm_init_complete_ntfy {
	__le32 status;
} __packed; /* PNVM_INIT_COMPLETE_NTFY_S_VER_1 */

#define UATS_TABLE_ROW_SIZE	26
#define UATS_TABLE_COL_SIZE	13

/**
 * struct iwl_mcc_allowed_ap_type_cmd - struct for MCC_ALLOWED_AP_TYPE_CMD
 * @offset_map: mapping a mcc to UHB AP type support (UATS) allowed
 * @reserved: reserved
 */
struct iwl_mcc_allowed_ap_type_cmd {
	u8 offset_map[UATS_TABLE_ROW_SIZE][UATS_TABLE_COL_SIZE];
	__le16 reserved;
} __packed; /* MCC_ALLOWED_AP_TYPE_CMD_API_S_VER_1 */

#endif /* __iwl_fw_api_nvm_reg_h__ */
