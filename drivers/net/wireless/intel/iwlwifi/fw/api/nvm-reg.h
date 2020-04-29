/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(C) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(C) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

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
	 * @NVM_GET_INFO:
	 * Command is &struct iwl_nvm_get_info,
	 * response is &struct iwl_nvm_get_info_rsp
	 */
	NVM_GET_INFO = 0x2,
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
	__le32 channels[0];
} __packed; /* LAR_UPDATE_MCC_CMD_RESP_S_VER_3 */

/**
 * struct iwl_mcc_update_resp - response to MCC_UPDATE_CMD.
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
struct iwl_mcc_update_resp {
	__le32 status;
	__le16 mcc;
	__le16 cap;
	__le16 time;
	__le16 geo_info;
	u8 source_id;
	u8 reserved[3];
	__le32 n_channels;
	__le32 channels[0];
} __packed; /* LAR_UPDATE_MCC_CMD_RESP_S_VER_4 */

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

#endif /* __iwl_fw_api_nvm_reg_h__ */
