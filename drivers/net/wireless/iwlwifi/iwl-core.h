/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 Intel Corporation. All rights reserved.
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
 * Tomas Winkler <tomas.winkler@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2008 Intel Corporation. All rights reserved.
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

#ifndef __iwl_core_h__
#define __iwl_core_h__

/************************
 * forward declarations *
 ************************/
struct iwl_host_cmd;
struct iwl_cmd;


#define IWLWIFI_VERSION "1.2.26k"
#define DRV_COPYRIGHT	"Copyright(c) 2003-2008 Intel Corporation"

#define IWL_PCI_DEVICE(dev, subdev, cfg) \
	.vendor = PCI_VENDOR_ID_INTEL,  .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = (subdev), \
	.driver_data = (kernel_ulong_t)&(cfg)

#define IWL_SKU_G       0x1
#define IWL_SKU_A       0x2
#define IWL_SKU_N       0x8

struct iwl_hcmd_ops {
	int (*rxon_assoc)(struct iwl_priv *priv);
};
struct iwl_hcmd_utils_ops {
	int (*enqueue_hcmd)(struct iwl_priv *priv, struct iwl_host_cmd *cmd);
};

struct iwl_lib_ops {
	/* iwlwifi driver (priv) init */
	int (*init_drv)(struct iwl_priv *priv);
	/* set hw dependant perameters */
	int (*set_hw_params)(struct iwl_priv *priv);

	void (*txq_update_byte_cnt_tbl)(struct iwl_priv *priv,
					struct iwl4965_tx_queue *txq,
					u16 byte_cnt);
	/* nic init */
	int (*hw_nic_init)(struct iwl_priv *priv);
	/* alive notification */
	int (*alive_notify)(struct iwl_priv *priv);
	/* check validity of rtc data address */
	int (*is_valid_rtc_data_addr)(u32 addr);
	/* 1st ucode load */
	int (*load_ucode)(struct iwl_priv *priv);
	/* rfkill */
	void (*radio_kill_sw)(struct iwl_priv *priv, int disable_radio);
	/* eeprom operations (as defined in iwl-eeprom.h) */
	struct iwl_eeprom_ops eeprom_ops;
};

struct iwl_ops {
	const struct iwl_lib_ops *lib;
	const struct iwl_hcmd_ops *hcmd;
	const struct iwl_hcmd_utils_ops *utils;
};

struct iwl_mod_params {
	int disable;		/* def: 0 = enable radio */
	int sw_crypto;		/* def: 0 = using hardware encryption */
	int debug;		/* def: 0 = minimal debug log messages */
	int disable_hw_scan;	/* def: 0 = use h/w scan */
	int num_of_queues;	/* def: HW dependent */
	int enable_qos;		/* def: 1 = use quality of service */
	int amsdu_size_8K;	/* def: 1 = enable 8K amsdu size */
	int antenna;  		/* def: 0 = both antennas (use diversity) */
};

struct iwl_cfg {
	const char *name;
	const char *fw_name;
	unsigned int sku;
	const struct iwl_ops *ops;
	const struct iwl_mod_params *mod_params;
};

/***************************
 *   L i b                 *
 ***************************/

struct ieee80211_hw *iwl_alloc_all(struct iwl_cfg *cfg,
		struct ieee80211_ops *hw_ops);

void iwlcore_clear_stations_table(struct iwl_priv *priv);
void iwlcore_reset_qos(struct iwl_priv *priv);
int iwlcore_set_rxon_channel(struct iwl_priv *priv,
				enum ieee80211_band band,
				u16 channel);

int iwl_setup(struct iwl_priv *priv);

/*****************************************************
 *   S e n d i n g     H o s t     C o m m a n d s   *
 *****************************************************/

const char *get_cmd_string(u8 cmd);
int __must_check iwl_send_cmd_sync(struct iwl_priv *priv,
				   struct iwl_host_cmd *cmd);
int iwl_send_cmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd);
int __must_check iwl_send_cmd_pdu(struct iwl_priv *priv, u8 id,
				  u16 len, const void *data);
int iwl_send_cmd_pdu_async(struct iwl_priv *priv, u8 id, u16 len,
			   const void *data,
			   int (*callback)(struct iwl_priv *priv,
					   struct iwl_cmd *cmd,
					   struct sk_buff *skb));
/*************** DRIVER STATUS FUNCTIONS   *****/

#define STATUS_HCMD_ACTIVE	0	/* host command in progress */
#define STATUS_HCMD_SYNC_ACTIVE	1	/* sync host command in progress */
#define STATUS_INT_ENABLED	2
#define STATUS_RF_KILL_HW	3
#define STATUS_RF_KILL_SW	4
#define STATUS_INIT		5
#define STATUS_ALIVE		6
#define STATUS_READY		7
#define STATUS_TEMPERATURE	8
#define STATUS_GEO_CONFIGURED	9
#define STATUS_EXIT_PENDING	10
#define STATUS_IN_SUSPEND	11
#define STATUS_STATISTICS	12
#define STATUS_SCANNING		13
#define STATUS_SCAN_ABORTING	14
#define STATUS_SCAN_HW		15
#define STATUS_POWER_PMI	16
#define STATUS_FW_ERROR		17
#define STATUS_CONF_PENDING	18


static inline int iwl_is_ready(struct iwl_priv *priv)
{
	/* The adapter is 'ready' if READY and GEO_CONFIGURED bits are
	 * set but EXIT_PENDING is not */
	return test_bit(STATUS_READY, &priv->status) &&
	       test_bit(STATUS_GEO_CONFIGURED, &priv->status) &&
	       !test_bit(STATUS_EXIT_PENDING, &priv->status);
}

static inline int iwl_is_alive(struct iwl_priv *priv)
{
	return test_bit(STATUS_ALIVE, &priv->status);
}

static inline int iwl_is_init(struct iwl_priv *priv)
{
	return test_bit(STATUS_INIT, &priv->status);
}

static inline int iwl_is_rfkill(struct iwl_priv *priv)
{
	return test_bit(STATUS_RF_KILL_HW, &priv->status) ||
	       test_bit(STATUS_RF_KILL_SW, &priv->status);
}

static inline int iwl_is_ready_rf(struct iwl_priv *priv)
{

	if (iwl_is_rfkill(priv))
		return 0;

	return iwl_is_ready(priv);
}


enum iwlcore_card_notify {
	IWLCORE_INIT_EVT = 0,
	IWLCORE_START_EVT = 1,
	IWLCORE_STOP_EVT = 2,
	IWLCORE_REMOVE_EVT = 3,
};

int iwlcore_low_level_notify(struct iwl_priv *priv,
			     enum iwlcore_card_notify notify);
extern int iwl_send_statistics_request(struct iwl_priv *priv, u8 flags);
int iwl_send_lq_cmd(struct iwl_priv *priv,
		    struct iwl_link_quality_cmd *lq, u8 flags);

static inline int iwl_send_rxon_assoc(struct iwl_priv *priv)
{
	return priv->cfg->ops->hcmd->rxon_assoc(priv);
}

#endif /* __iwl_core_h__ */
