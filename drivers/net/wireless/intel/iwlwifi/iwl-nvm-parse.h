/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2015 Intel Corporation. All rights reserved.
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
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
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
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
 *****************************************************************************/
#ifndef __iwl_nvm_parse_h__
#define __iwl_nvm_parse_h__

#include <net/cfg80211.h>
#include "iwl-eeprom-parse.h"

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
		       u16 geo_info, u16 cap, u8 resp_ver);

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

/**
 * iwl_get_nvm - retrieve NVM data from firmware
 *
 * Allocates a new iwl_nvm_data structure, fills it with
 * NVM data, and returns it to caller.
 */
struct iwl_nvm_data *iwl_get_nvm(struct iwl_trans *trans,
				 const struct iwl_fw *fw);
#endif /* __iwl_nvm_parse_h__ */
