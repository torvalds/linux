/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014, 2018, 2020-2022 Intel Corporation
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 */
#ifndef __iwl_eeprom_parse_h__
#define __iwl_eeprom_parse_h__

#include <linux/types.h>
#include <linux/if_ether.h>
#include <net/cfg80211.h>
#include "iwl-trans.h"

struct iwl_nvm_data {
	int n_hw_addrs;
	u8 hw_addr[ETH_ALEN];

	u8 calib_version;
	__le16 calib_voltage;

	__le16 raw_temperature;
	__le16 kelvin_temperature;
	__le16 kelvin_voltage;
	__le16 xtal_calib[2];

	bool sku_cap_band_24ghz_enable;
	bool sku_cap_band_52ghz_enable;
	bool sku_cap_11n_enable;
	bool sku_cap_11ac_enable;
	bool sku_cap_11ax_enable;
	bool sku_cap_amt_enable;
	bool sku_cap_ipan_enable;
	bool sku_cap_mimo_disabled;
	bool sku_cap_11be_enable;

	u16 radio_cfg_type;
	u8 radio_cfg_step;
	u8 radio_cfg_dash;
	u8 radio_cfg_pnum;
	u8 valid_tx_ant, valid_rx_ant;

	u32 nvm_version;
	s8 max_tx_pwr_half_dbm;

	bool lar_enabled;
	bool vht160_supported;
	struct ieee80211_supported_band bands[NUM_NL80211_BANDS];

	/*
	 * iftype data for low (2.4 GHz) high (5 GHz) and uhb (6 GHz) bands
	 */
	struct {
		struct ieee80211_sband_iftype_data low[2];
		struct ieee80211_sband_iftype_data high[2];
		struct ieee80211_sband_iftype_data uhb[2];
	} iftd;

	struct ieee80211_channel channels[];
};

/**
 * iwl_parse_eeprom_data - parse EEPROM data and return values
 *
 * @dev: device pointer we're parsing for, for debug only
 * @cfg: device configuration for parsing and overrides
 * @eeprom: the EEPROM data
 * @eeprom_size: length of the EEPROM data
 *
 * This function parses all EEPROM values we need and then
 * returns a (newly allocated) struct containing all the
 * relevant values for driver use. The struct must be freed
 * later with iwl_free_nvm_data().
 */
struct iwl_nvm_data *
iwl_parse_eeprom_data(struct iwl_trans *trans, const struct iwl_cfg *cfg,
		      const u8 *eeprom, size_t eeprom_size);

int iwl_init_sband_channels(struct iwl_nvm_data *data,
			    struct ieee80211_supported_band *sband,
			    int n_channels, enum nl80211_band band);

void iwl_init_ht_hw_capab(struct iwl_trans *trans,
			  struct iwl_nvm_data *data,
			  struct ieee80211_sta_ht_cap *ht_info,
			  enum nl80211_band band,
			  u8 tx_chains, u8 rx_chains);

#endif /* __iwl_eeprom_parse_h__ */
