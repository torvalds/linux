/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2015, 2018-2025 Intel Corporation
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_nvm_parse_h__
#define __iwl_nvm_parse_h__

#include <net/cfg80211.h>
#include "iwl-nvm-utils.h"
#include "mei/iwl-mei.h"

/**
 * enum iwl_nvm_sbands_flags - modification flags for the channel profiles
 *
 * @IWL_NVM_SBANDS_FLAGS_LAR: LAR is enabled
 * @IWL_NVM_SBANDS_FLAGS_NO_WIDE_IN_5GHZ: disallow 40, 80 and 160MHz on 5GHz
 */
enum iwl_nvm_sbands_flags {
	IWL_NVM_SBANDS_FLAGS_LAR		= BIT(0),
	IWL_NVM_SBANDS_FLAGS_NO_WIDE_IN_5GHZ	= BIT(1),
};

/**
 * struct iwl_reg_capa - struct for global regulatory capabilities, Used for
 * handling the different APIs of reg_capa_flags.
 *
 * @allow_40mhz: 11n channel with a width of 40Mhz is allowed
 *	for this regulatory domain.
 * @allow_80mhz: 11ac channel with a width of 80Mhz is allowed
 *	for this regulatory domain (valid only in 5 and 6 Ghz).
 * @allow_160mhz: 11ac channel with a width of 160Mhz is allowed
 *	for this regulatory domain (valid only in 5 and 6 Ghz).
 * @allow_320mhz: 11be channel with a width of 320Mhz is allowed
 *	for this regulatory domain (valid only in 6 Ghz).
 * @disable_11ax: 11ax is forbidden for this regulatory domain.
 * @disable_11be: 11be is forbidden for this regulatory domain.
 */
struct iwl_reg_capa {
	bool allow_40mhz;
	bool allow_80mhz;
	bool allow_160mhz;
	bool allow_320mhz;
	bool disable_11ax;
	bool disable_11be;
};

/**
 * enum iwl_nvm_channel_flags - channel flags in NVM
 * @NVM_CHANNEL_VALID: channel is usable for this SKU/geo
 * @NVM_CHANNEL_IBSS: usable as an IBSS channel and deprecated
 *	when %IWL_NVM_SBANDS_FLAGS_LAR enabled.
 * @NVM_CHANNEL_ALLOW_20MHZ_ACTIVITY: active scanning allowed and
 *	AP allowed only in 20 MHz. Valid only
 *	when %IWL_NVM_SBANDS_FLAGS_LAR enabled.
 * @NVM_CHANNEL_ACTIVE: active scanning allowed and allows IBSS
 *	when %IWL_NVM_SBANDS_FLAGS_LAR enabled.
 * @NVM_CHANNEL_RADAR: radar detection required
 * @NVM_CHANNEL_INDOOR_ONLY: only indoor use is allowed
 * @NVM_CHANNEL_GO_CONCURRENT: GO operation is allowed when connected to BSS
 *	on same channel on 2.4 or same UNII band on 5.2
 * @NVM_CHANNEL_UNIFORM: uniform spreading required
 * @NVM_CHANNEL_20MHZ: 20 MHz channel okay
 * @NVM_CHANNEL_40MHZ: 40 MHz channel okay
 * @NVM_CHANNEL_80MHZ: 80 MHz channel okay
 * @NVM_CHANNEL_160MHZ: 160 MHz channel okay
 * @NVM_CHANNEL_DC_HIGH: DC HIGH required/allowed (?)
 * @NVM_CHANNEL_VLP: client support connection to UHB VLP AP
 * @NVM_CHANNEL_AFC: client support connection to UHB AFC AP
 * @NVM_CHANNEL_VLP_AP_NOT_ALLOWED: UHB VLP AP not allowed,
 *	Valid only when %NVM_CHANNEL_VLP is enabled.
 */
enum iwl_nvm_channel_flags {
	NVM_CHANNEL_VALID			= BIT(0),
	NVM_CHANNEL_IBSS			= BIT(1),
	NVM_CHANNEL_ALLOW_20MHZ_ACTIVITY	= BIT(2),
	NVM_CHANNEL_ACTIVE			= BIT(3),
	NVM_CHANNEL_RADAR			= BIT(4),
	NVM_CHANNEL_INDOOR_ONLY			= BIT(5),
	NVM_CHANNEL_GO_CONCURRENT		= BIT(6),
	NVM_CHANNEL_UNIFORM			= BIT(7),
	NVM_CHANNEL_20MHZ			= BIT(8),
	NVM_CHANNEL_40MHZ			= BIT(9),
	NVM_CHANNEL_80MHZ			= BIT(10),
	NVM_CHANNEL_160MHZ			= BIT(11),
	NVM_CHANNEL_DC_HIGH			= BIT(12),
	NVM_CHANNEL_VLP				= BIT(13),
	NVM_CHANNEL_AFC				= BIT(14),
	NVM_CHANNEL_VLP_AP_NOT_ALLOWED		= BIT(15),
};

#if IS_ENABLED(CONFIG_IWLWIFI_KUNIT_TESTS)
u32 iwl_nvm_get_regdom_bw_flags(const u16 *nvm_chan,
				int ch_idx, u16 nvm_flags,
				struct iwl_reg_capa reg_capa);
#endif

/*
 * iwl_parse_nvm_data - parse NVM data and return values
 *
 * This function parses all NVM values we need and then
 * returns a (newly allocated) struct containing all the
 * relevant values for driver use. The struct must be freed
 * later with iwl_free_nvm_data().
 */
struct iwl_nvm_data *
iwl_parse_nvm_data(struct iwl_trans *trans, const struct iwl_rf_cfg *cfg,
		   const struct iwl_fw *fw,
		   const __be16 *nvm_hw, const __le16 *nvm_sw,
		   const __le16 *nvm_calib, const __le16 *regulatory,
		   const __le16 *mac_override, const __le16 *phy_sku,
		   u8 tx_chains, u8 rx_chains);

/**
 * iwl_parse_nvm_mcc_info - parse MCC (mobile country code) info coming from FW
 *
 * This function parses the regulatory channel data received as a
 * MCC_UPDATE_CMD command. It returns a newly allocation regulatory domain,
 * to be fed into the regulatory core. In case the geo_info is set handle
 * accordingly. An ERR_PTR is returned on error.
 * If not given to the regulatory core, the user is responsible for freeing
 * the regdomain returned here with kfree.
 *
 * @trans: the transport
 * @num_of_ch: the number of channels
 * @channels: channel map
 * @fw_mcc: firmware country code
 * @geo_info: geo info value
 * @cap: capability
 * @resp_ver: FW response version
 */
struct ieee80211_regdomain *
iwl_parse_nvm_mcc_info(struct iwl_trans *trans,
		       int num_of_ch, __le32 *channels, u16 fw_mcc,
		       u16 geo_info, u32 cap, u8 resp_ver);

/**
 * struct iwl_nvm_section - describes an NVM section in memory.
 *
 * This struct holds an NVM section read from the NIC using NVM_ACCESS_CMD,
 * and saved for later use by the driver. Not all NVM sections are saved
 * this way, only the needed ones.
 */
struct iwl_nvm_section {
	u16 length;
	const u8 *data;
};

/**
 * iwl_read_external_nvm - Reads external NVM from a file into nvm_sections
 */
int iwl_read_external_nvm(struct iwl_trans *trans,
			  const char *nvm_file_name,
			  struct iwl_nvm_section *nvm_sections);
void iwl_nvm_fixups(u32 hw_id, unsigned int section, u8 *data,
		    unsigned int len);

/*
 * iwl_get_nvm - retrieve NVM data from firmware
 *
 * Allocates a new iwl_nvm_data structure, fills it with
 * NVM data, and returns it to caller.
 */
struct iwl_nvm_data *iwl_get_nvm(struct iwl_trans *trans,
				 const struct iwl_fw *fw,
				 u8 set_tx_ant, u8 set_rx_ant);

/*
 * iwl_parse_mei_nvm_data - parse the mei_nvm_data and get an iwl_nvm_data
 */
struct iwl_nvm_data *
iwl_parse_mei_nvm_data(struct iwl_trans *trans, const struct iwl_rf_cfg *cfg,
		       const struct iwl_mei_nvm *mei_nvm,
		       const struct iwl_fw *fw, u8 set_tx_ant, u8 set_rx_ant);

/*
 * iwl_reinit_cab - to be called when the tx_chains or rx_chains are modified
 */
void iwl_reinit_cab(struct iwl_trans *trans, struct iwl_nvm_data *data,
		    u8 tx_chains, u8 rx_chains, const struct iwl_fw *fw);

#endif /* __iwl_nvm_parse_h__ */
