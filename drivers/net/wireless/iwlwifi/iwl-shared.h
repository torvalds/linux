/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2012 Intel Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2012 Intel Corporation. All rights reserved.
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
#ifndef __iwl_shared_h__
#define __iwl_shared_h__

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>
#include <net/mac80211.h>

#include "iwl-commands.h"
#include "iwl-fw.h"

/**
 * DOC: shared area - role and goal
 *
 * The shared area contains all the data exported by the upper layer to the
 * other layers. Since the bus and transport layer shouldn't dereference
 * iwl_priv, all the data needed by the upper layer and the transport / bus
 * layer must be here.
 * The shared area also holds pointer to all the other layers. This allows a
 * layer to call a function from another layer.
 *
 * NOTE: All the layers hold a pointer to the shared area which must be shrd.
 *	A few macros assume that (_m)->shrd points to the shared area no matter
 *	what _m is.
 *
 * gets notifications about enumeration, suspend, resume.
 * For the moment, the bus layer is not a linux kernel module as itself, and
 * the module_init function of the driver must call the bus specific
 * registration functions. These functions are listed at the end of this file.
 * For the moment, there is only one implementation of this interface: PCI-e.
 * This implementation is iwl-pci.c
 */

struct iwl_priv;
struct iwl_trans;
struct iwl_sensitivity_ranges;
struct iwl_trans_ops;

#define DRV_NAME        "iwlwifi"
#define IWLWIFI_VERSION "in-tree:"
#define DRV_COPYRIGHT	"Copyright(c) 2003-2012 Intel Corporation"
#define DRV_AUTHOR     "<ilw@linux.intel.com>"

extern struct iwl_mod_params iwlagn_mod_params;

#define IWL_DISABLE_HT_ALL	BIT(0)
#define IWL_DISABLE_HT_TXAGG	BIT(1)
#define IWL_DISABLE_HT_RXAGG	BIT(2)

/**
 * struct iwl_mod_params
 *
 * Holds the module parameters
 *
 * @sw_crypto: using hardware encryption, default = 0
 * @disable_11n: disable 11n capabilities, default = 0,
 *	use IWL_DISABLE_HT_* constants
 * @amsdu_size_8K: enable 8K amsdu size, default = 1
 * @antenna: both antennas (use diversity), default = 0
 * @restart_fw: restart firmware, default = 1
 * @plcp_check: enable plcp health check, default = true
 * @ack_check: disable ack health check, default = false
 * @wd_disable: enable stuck queue check, default = 0
 * @bt_coex_active: enable bt coex, default = true
 * @led_mode: system default, default = 0
 * @no_sleep_autoadjust: disable autoadjust, default = true
 * @power_save: disable power save, default = false
 * @power_level: power level, default = 1
 * @debug_level: levels are IWL_DL_*
 * @ant_coupling: antenna coupling in dB, default = 0
 * @bt_ch_announce: BT channel inhibition, default = enable
 * @wanted_ucode_alternative: ucode alternative to use, default = 1
 * @auto_agg: enable agg. without check, default = true
 */
struct iwl_mod_params {
	int sw_crypto;
	unsigned int disable_11n;
	int amsdu_size_8K;
	int antenna;
	int restart_fw;
	bool plcp_check;
	bool ack_check;
	int  wd_disable;
	bool bt_coex_active;
	int led_mode;
	bool no_sleep_autoadjust;
	bool power_save;
	int power_level;
	u32 debug_level;
	int ant_coupling;
	bool bt_ch_announce;
	int wanted_ucode_alternative;
	bool auto_agg;
};

/**
 * struct iwl_hw_params
 *
 * Holds the module parameters
 *
 * @num_ampdu_queues: num of ampdu queues
 * @tx_chains_num: Number of TX chains
 * @rx_chains_num: Number of RX chains
 * @valid_tx_ant: usable antennas for TX
 * @valid_rx_ant: usable antennas for RX
 * @ht40_channel: is 40MHz width possible: BIT(IEEE80211_BAND_XXX)
 * @sku: sku read from EEPROM
 * @rx_page_order: Rx buffer page order
 * @ct_kill_threshold: temperature threshold - in hw dependent unit
 * @ct_kill_exit_threshold: when to reeable the device - in hw dependent unit
 *	relevant for 1000, 6000 and up
 * @wd_timeout: TX queues watchdog timeout
 * @struct iwl_sensitivity_ranges: range of sensitivity values
 * @use_rts_for_aggregation: use rts/cts protection for HT traffic
 */
struct iwl_hw_params {
	u8  num_ampdu_queues;
	u8  tx_chains_num;
	u8  rx_chains_num;
	u8  valid_tx_ant;
	u8  valid_rx_ant;
	u8  ht40_channel;
	bool use_rts_for_aggregation;
	u16 sku;
	u32 rx_page_order;
	u32 ct_kill_threshold;
	u32 ct_kill_exit_threshold;
	unsigned int wd_timeout;

	const struct iwl_sensitivity_ranges *sens;
};

/*
 * LED mode
 *    IWL_LED_DEFAULT:  use device default
 *    IWL_LED_RF_STATE: turn LED on/off based on RF state
 *			LED ON  = RF ON
 *			LED OFF = RF OFF
 *    IWL_LED_BLINK:    adjust led blink rate based on blink table
 *    IWL_LED_DISABLE:	led disabled
 */
enum iwl_led_mode {
	IWL_LED_DEFAULT,
	IWL_LED_RF_STATE,
	IWL_LED_BLINK,
	IWL_LED_DISABLE,
};

/*
 * @max_ll_items: max number of OTP blocks
 * @shadow_ram_support: shadow support for OTP memory
 * @led_compensation: compensate on the led on/off time per HW according
 *	to the deviation to achieve the desired led frequency.
 *	The detail algorithm is described in iwl-led.c
 * @chain_noise_num_beacons: number of beacons used to compute chain noise
 * @adv_thermal_throttle: support advance thermal throttle
 * @support_ct_kill_exit: support ct kill exit condition
 * @support_wimax_coexist: support wimax/wifi co-exist
 * @plcp_delta_threshold: plcp error rate threshold used to trigger
 *	radio tuning when there is a high receiving plcp error rate
 * @chain_noise_scale: default chain noise scale used for gain computation
 * @wd_timeout: TX queues watchdog timeout
 * @max_event_log_size: size of event log buffer size for ucode event logging
 * @shadow_reg_enable: HW shadhow register bit
 * @hd_v2: v2 of enhanced sensitivity value, used for 2000 series and up
 * @no_idle_support: do not support idle mode
 * wd_disable: disable watchdog timer
 */
struct iwl_base_params {
	int eeprom_size;
	int num_of_queues;	/* def: HW dependent */
	int num_of_ampdu_queues;/* def: HW dependent */
	/* for iwl_apm_init() */
	u32 pll_cfg_val;

	const u16 max_ll_items;
	const bool shadow_ram_support;
	u16 led_compensation;
	bool adv_thermal_throttle;
	bool support_ct_kill_exit;
	const bool support_wimax_coexist;
	u8 plcp_delta_threshold;
	s32 chain_noise_scale;
	unsigned int wd_timeout;
	u32 max_event_log_size;
	const bool shadow_reg_enable;
	const bool hd_v2;
	const bool no_idle_support;
	const bool wd_disable;
};

/*
 * @advanced_bt_coexist: support advanced bt coexist
 * @bt_init_traffic_load: specify initial bt traffic load
 * @bt_prio_boost: default bt priority boost value
 * @agg_time_limit: maximum number of uSec in aggregation
 * @bt_sco_disable: uCode should not response to BT in SCO/ESCO mode
 */
struct iwl_bt_params {
	bool advanced_bt_coexist;
	u8 bt_init_traffic_load;
	u8 bt_prio_boost;
	u16 agg_time_limit;
	bool bt_sco_disable;
	bool bt_session_2;
};
/*
 * @use_rts_for_aggregation: use rts/cts protection for HT traffic
 */
struct iwl_ht_params {
	const bool ht_greenfield_support; /* if used set to true */
	bool use_rts_for_aggregation;
	enum ieee80211_smps_mode smps_mode;
};

/**
 * struct iwl_cfg
 * @name: Offical name of the device
 * @fw_name_pre: Firmware filename prefix. The api version and extension
 *	(.ucode) will be added to filename before loading from disk. The
 *	filename is constructed as fw_name_pre<api>.ucode.
 * @ucode_api_max: Highest version of uCode API supported by driver.
 * @ucode_api_ok: oldest version of the uCode API that is OK to load
 *	without a warning, for use in transitions
 * @ucode_api_min: Lowest version of uCode API supported by driver.
 * @max_inst_size: The maximal length of the fw inst section
 * @max_data_size: The maximal length of the fw data section
 * @valid_tx_ant: valid transmit antenna
 * @valid_rx_ant: valid receive antenna
 * @eeprom_ver: EEPROM version
 * @eeprom_calib_ver: EEPROM calibration version
 * @lib: pointer to the lib ops
 * @additional_nic_config: additional nic configuration
 * @base_params: pointer to basic parameters
 * @ht_params: point to ht patameters
 * @bt_params: pointer to bt parameters
 * @need_temp_offset_calib: need to perform temperature offset calibration
 * @no_xtal_calib: some devices do not need crystal calibration data,
 *	don't send it to those
 * @scan_rx_antennas: available antenna for scan operation
 * @led_mode: 0=blinking, 1=On(RF On)/Off(RF Off)
 * @adv_pm: advance power management
 * @rx_with_siso_diversity: 1x1 device with rx antenna diversity
 * @internal_wimax_coex: internal wifi/wimax combo device
 * @iq_invert: I/Q inversion
 * @temp_offset_v2: support v2 of temperature offset calibration
 *
 * We enable the driver to be backward compatible wrt API version. The
 * driver specifies which APIs it supports (with @ucode_api_max being the
 * highest and @ucode_api_min the lowest). Firmware will only be loaded if
 * it has a supported API version.
 *
 * The ideal usage of this infrastructure is to treat a new ucode API
 * release as a new hardware revision.
 */
struct iwl_cfg {
	/* params specific to an individual device within a device family */
	const char *name;
	const char *fw_name_pre;
	const unsigned int ucode_api_max;
	const unsigned int ucode_api_ok;
	const unsigned int ucode_api_min;
	const u32 max_data_size;
	const u32 max_inst_size;
	u8   valid_tx_ant;
	u8   valid_rx_ant;
	u16  eeprom_ver;
	u16  eeprom_calib_ver;
	const struct iwl_lib_ops *lib;
	void (*additional_nic_config)(struct iwl_priv *priv);
	/* params not likely to change within a device family */
	const struct iwl_base_params *base_params;
	/* params likely to change within a device family */
	const struct iwl_ht_params *ht_params;
	const struct iwl_bt_params *bt_params;
	const bool need_temp_offset_calib; /* if used set to true */
	const bool no_xtal_calib;
	u8 scan_rx_antennas[IEEE80211_NUM_BANDS];
	enum iwl_led_mode led_mode;
	const bool adv_pm;
	const bool rx_with_siso_diversity;
	const bool internal_wimax_coex;
	const bool iq_invert;
	const bool temp_offset_v2;
};

/**
 * struct iwl_shared - shared fields for all the layers of the driver
 *
 * @status: STATUS_*
 * @wowlan: are we running wowlan uCode
 * @valid_contexts: microcode/device supports multiple contexts
 * @bus: pointer to the bus layer data
 * @cfg: see struct iwl_cfg
 * @priv: pointer to the upper layer data
 * @trans: pointer to the transport layer data
 * @nic: pointer to the nic data
 * @hw_params: see struct iwl_hw_params
 * @lock: protect general shared data
 * @eeprom: pointer to the eeprom/OTP image
 * @ucode_type: indicator of loaded ucode image
 * @device_pointers: pointers to ucode event tables
 */
struct iwl_shared {
	unsigned long status;
	u8 valid_contexts;

	const struct iwl_cfg *cfg;
	struct iwl_trans *trans;
	void *drv;
	struct iwl_hw_params hw_params;
	const struct iwl_fw *fw;

	/* eeprom -- this is in the card's little endian byte order */
	u8 *eeprom;

	/* ucode related variables */
	enum iwl_ucode_type ucode_type;

	struct {
		u32 error_event_table;
		u32 log_event_table;
	} device_pointers;

};

/*Whatever _m is (iwl_trans, iwl_priv, these macros will work */
#define cfg(_m)		((_m)->shrd->cfg)
#define trans(_m)	((_m)->shrd->trans)
#define hw_params(_m)	((_m)->shrd->hw_params)

static inline bool iwl_have_debug_level(u32 level)
{
	return iwlagn_mod_params.debug_level & level;
}

enum iwl_rxon_context_id {
	IWL_RXON_CTX_BSS,
	IWL_RXON_CTX_PAN,

	NUM_IWL_RXON_CTX
};

int iwlagn_hw_valid_rtc_data_addr(u32 addr);
const char *get_cmd_string(u8 cmd);

#define IWL_CMD(x) case x: return #x

/*****************************************************
* DRIVER STATUS FUNCTIONS
******************************************************/
#define STATUS_HCMD_ACTIVE	0	/* host command in progress */
/* 1 is unused (used to be STATUS_HCMD_SYNC_ACTIVE) */
#define STATUS_INT_ENABLED	2
#define STATUS_RF_KILL_HW	3
#define STATUS_CT_KILL		4
#define STATUS_INIT		5
#define STATUS_ALIVE		6
#define STATUS_READY		7
#define STATUS_TEMPERATURE	8
#define STATUS_GEO_CONFIGURED	9
#define STATUS_EXIT_PENDING	10
#define STATUS_STATISTICS	12
#define STATUS_SCANNING		13
#define STATUS_SCAN_ABORTING	14
#define STATUS_SCAN_HW		15
#define STATUS_POWER_PMI	16
#define STATUS_FW_ERROR		17
#define STATUS_DEVICE_ENABLED	18
#define STATUS_CHANNEL_SWITCH_PENDING 19
#define STATUS_SCAN_COMPLETE	20

#endif /* #__iwl_shared_h__ */
