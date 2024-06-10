/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2015, 2018-2023 Intel Corporation
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_nvm_parse_h__
#define __iwl_nvm_parse_h__

#include <net/cfg80211.h>
#include "iwl-eeprom-parse.h"
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

/*
 * iwl_parse_nvm_data - parse NVM data and return values
 *
 * This function parses all NVM values we need and then
 * returns a (newly allocated) struct containing all the
 * relevant values for driver use. The struct must be freed
 * later with iwl_free_nvm_data().
 */
struct iwl_nvm_data *
iwl_parse_nvm_data(struct iwl_trans *trans, const struct iwl_cfg *cfg,
		   const struct iwl_fw *fw,
		   const __be16 *nvm_hw, const __le16 *nvm_sw,
		   const __le16 *nvm_calib, const __le16 *regulatory,
		   const __le16 *mac_override, const __le16 *phy_sku,
		   u8 tx_chains, u8 rx_chains);

/**
 * iwl_parse_mcc_info - parse MCC (mobile country code) info coming from FW
 *
 * This function parses the regulatory channel data received as a
 * MCC_UPDATE_CMD command. It returns a newly allocation regulatory domain,
 * to be fed into the regulatory core. In case the geo_info is set handle
 * accordingly. An ERR_PTR is returned on error.
 * If not given to the regulatory core, the user is responsible for freeing
 * the regdomain returned here with kfree.
 */
struct ieee80211_regdomain *
iwl_parse_nvm_mcc_info(struct device *dev, const struct iwl_cfg *cfg,
		       int num_of_ch, __le32 *channels, u16 fw_mcc,
		       u16 geo_info, u32 cap, u8 resp_ver, bool uats_enabled);

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
iwl_parse_mei_nvm_data(struct iwl_trans *trans, const struct iwl_cfg *cfg,
		       const struct iwl_mei_nvm *mei_nvm,
		       const struct iwl_fw *fw, u8 set_tx_ant, u8 set_rx_ant);

/*
 * iwl_reinit_cab - to be called when the tx_chains or rx_chains are modified
 */
void iwl_reinit_cab(struct iwl_trans *trans, struct iwl_nvm_data *data,
		    u8 tx_chains, u8 rx_chains, const struct iwl_fw *fw);

#endif /* __iwl_nvm_parse_h__ */
