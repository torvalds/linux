/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2011 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
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

struct iwl_cfg;
struct iwl_bus;
struct iwl_priv;
struct iwl_sensitivity_ranges;

extern struct iwl_mod_params iwlagn_mod_params;

struct iwl_mod_params {
	int sw_crypto;		/* def: 0 = using hardware encryption */
	int num_of_queues;	/* def: HW dependent */
	int disable_11n;	/* def: 0 = 11n capabilities enabled */
	int amsdu_size_8K;	/* def: 1 = enable 8K amsdu size */
	int antenna;		/* def: 0 = both antennas (use diversity) */
	int restart_fw;		/* def: 1 = restart firmware */
	bool plcp_check;	/* def: true = enable plcp health check */
	bool ack_check;		/* def: false = disable ack health check */
	bool wd_disable;	/* def: false = enable stuck queue check */
	bool bt_coex_active;	/* def: true = enable bt coex */
	int led_mode;		/* def: 0 = system default */
	bool no_sleep_autoadjust; /* def: true = disable autoadjust */
	bool power_save;	/* def: false = disable power save */
	int power_level;	/* def: 1 = power level */
	u32 debug_level;	/* levels are IWL_DL_* */
	int ant_coupling;
	bool bt_ch_announce;
	int wanted_ucode_alternative;
};

/**
 * struct iwl_hw_params
 * @max_txq_num: Max # Tx queues supported
 * @scd_bc_tbls_size: size of scheduler byte count tables
 * @tfd_size: TFD size
 * @tx/rx_chains_num: Number of TX/RX chains
 * @valid_tx/rx_ant: usable antennas
 * @max_rxq_size: Max # Rx frames in Rx queue (must be power-of-2)
 * @max_rxq_log: Log-base-2 of max_rxq_size
 * @rx_page_order: Rx buffer page order
 * @rx_wrt_ptr_reg: FH{39}_RSCSR_CHNL0_WPTR
 * @max_stations:
 * @ht40_channel: is 40MHz width possible in band 2.4
 * BIT(IEEE80211_BAND_5GHZ) BIT(IEEE80211_BAND_5GHZ)
 * @sw_crypto: 0 for hw, 1 for sw
 * @max_xxx_size: for ucode uses
 * @ct_kill_threshold: temperature threshold
 * @beacon_time_tsf_bits: number of valid tsf bits for beacon time
 * @calib_init_cfg: setup initial calibrations for the hw
 * @calib_rt_cfg: setup runtime calibrations for the hw
 * @struct iwl_sensitivity_ranges: range of sensitivity values
 */
struct iwl_hw_params {
	u8 max_txq_num;
	u16 scd_bc_tbls_size;
	u32 tfd_size;
	u8  tx_chains_num;
	u8  rx_chains_num;
	u8  valid_tx_ant;
	u8  valid_rx_ant;
	u16 max_rxq_size;
	u16 max_rxq_log;
	u32 rx_page_order;
	u8  max_stations;
	u8  ht40_channel;
	u8  max_beacon_itrvl;	/* in 1024 ms */
	u32 max_inst_size;
	u32 max_data_size;
	u32 ct_kill_threshold; /* value in hw-dependent units */
	u32 ct_kill_exit_threshold; /* value in hw-dependent units */
				    /* for 1000, 6000 series and up */
	u16 beacon_time_tsf_bits;
	u32 calib_init_cfg;
	u32 calib_rt_cfg;
	const struct iwl_sensitivity_ranges *sens;
};

/**
 * struct iwl_shared - shared fields for all the layers of the driver
 *
 * @dbg_level_dev: dbg level set per device. Prevails on
 *	iwlagn_mod_params.debug_level if set (!= 0)
 * @bus: pointer to the bus layer data
 * @priv: pointer to the upper layer data
 * @hw_params: see struct iwl_hw_params
 */
struct iwl_shared {
#ifdef CONFIG_IWLWIFI_DEBUG
	u32 dbg_level_dev;
#endif /* CONFIG_IWLWIFI_DEBUG */

	struct iwl_bus *bus;
	struct iwl_priv *priv;
	struct iwl_hw_params hw_params;
};

/*Whatever _m is (iwl_trans, iwl_priv, iwl_bus, these macros will work */
#define priv(_m)	((_m)->shrd->priv)
#define bus(_m)		((_m)->shrd->bus)
#define hw_params(_m)	((_m)->shrd->hw_params)

#ifdef CONFIG_IWLWIFI_DEBUG
/*
 * iwl_get_debug_level: Return active debug level for device
 *
 * Using sysfs it is possible to set per device debug level. This debug
 * level will be used if set, otherwise the global debug level which can be
 * set via module parameter is used.
 */
static inline u32 iwl_get_debug_level(struct iwl_shared *shrd)
{
	if (shrd->dbg_level_dev)
		return shrd->dbg_level_dev;
	else
		return iwlagn_mod_params.debug_level;
}
#else
static inline u32 iwl_get_debug_level(struct iwl_shared *shrd)
{
	return iwlagn_mod_params.debug_level;
}
#endif

#ifdef CONFIG_PM
int iwl_suspend(struct iwl_priv *priv);
int iwl_resume(struct iwl_priv *priv);
#endif /* !CONFIG_PM */

int iwl_probe(struct iwl_bus *bus, struct iwl_cfg *cfg);
void __devexit iwl_remove(struct iwl_priv * priv);

#endif /* #__iwl_shared_h__ */
