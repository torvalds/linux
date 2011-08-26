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

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <net/mac80211.h>

#include "iwl-commands.h"

/*This files includes all the types / functions that are exported by the
 * upper layer to the bus and transport layer */

struct iwl_cfg;
struct iwl_bus;
struct iwl_priv;
struct iwl_sensitivity_ranges;
struct iwl_trans_ops;

#define DRV_NAME        "iwlagn"
#define IWLWIFI_VERSION "in-tree:"
#define DRV_COPYRIGHT	"Copyright(c) 2003-2011 Intel Corporation"
#define DRV_AUTHOR     "<ilw@linux.intel.com>"

extern struct iwl_mod_params iwlagn_mod_params;

/**
 * struct iwl_mod_params
 * @sw_crypto: using hardware encryption, default = 0
 * @num_of_queues: number of tx queue, HW dependent
 * @disable_11n: 11n capabilities enabled, default = 0
 * @amsdu_size_8K: enable 8K amsdu size, default = 1
 * @antenna: both antennas (use diversity), default = 0
 * @restart_fw: restart firmware, default = 1
 * @plcp_check: enable plcp health check, default = true
 * @ack_check: disable ack health check, default = false
 * @wd_disable: enable stuck queue check, default = false
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
	int num_of_queues;
	int disable_11n;
	int amsdu_size_8K;
	int antenna;
	int restart_fw;
	bool plcp_check;
	bool ack_check;
	bool wd_disable;
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
 * @max_txq_num: Max # Tx queues supported
 * @num_ampdu_queues: num of ampdu queues
 * @tx/rx_chains_num: Number of TX/RX chains
 * @valid_tx/rx_ant: usable antennas
 * @max_stations:
 * @ht40_channel: is 40MHz width possible in band 2.4
 * @beacon_time_tsf_bits: number of valid tsf bits for beacon time
 * @sku:
 * @rx_page_order: Rx buffer page order
 * @rx_wrt_ptr_reg: FH{39}_RSCSR_CHNL0_WPTR
 * BIT(IEEE80211_BAND_5GHZ) BIT(IEEE80211_BAND_5GHZ)
 * @sw_crypto: 0 for hw, 1 for sw
 * @max_xxx_size: for ucode uses
 * @ct_kill_threshold: temperature threshold
 * @calib_init_cfg: setup initial calibrations for the hw
 * @calib_rt_cfg: setup runtime calibrations for the hw
 * @struct iwl_sensitivity_ranges: range of sensitivity values
 */
struct iwl_hw_params {
	u8  max_txq_num;
	u8  num_ampdu_queues;
	u8  tx_chains_num;
	u8  rx_chains_num;
	u8  valid_tx_ant;
	u8  valid_rx_ant;
	u8  max_stations;
	u8  ht40_channel;
	bool shadow_reg_enable;
	u16 beacon_time_tsf_bits;
	u16 sku;
	u32 rx_page_order;
	u32 max_inst_size;
	u32 max_data_size;
	u32 ct_kill_threshold; /* value in hw-dependent units */
	u32 ct_kill_exit_threshold; /* value in hw-dependent units */
				    /* for 1000, 6000 series and up */
	u32 calib_init_cfg;
	u32 calib_rt_cfg;
	const struct iwl_sensitivity_ranges *sens;
};

/**
 * struct iwl_ht_agg - aggregation status while waiting for block-ack
 * @txq_id: Tx queue used for Tx attempt
 * @wait_for_ba: Expect block-ack before next Tx reply
 * @rate_n_flags: Rate at which Tx was attempted
 *
 * If REPLY_TX indicates that aggregation was attempted, driver must wait
 * for block ack (REPLY_COMPRESSED_BA).  This struct stores tx reply info
 * until block ack arrives.
 */
struct iwl_ht_agg {
	u16 txq_id;
	u16 wait_for_ba;
	u32 rate_n_flags;
#define IWL_AGG_OFF 0
#define IWL_AGG_ON 1
#define IWL_EMPTYING_HW_QUEUE_ADDBA 2
#define IWL_EMPTYING_HW_QUEUE_DELBA 3
	u8 state;
};

struct iwl_tid_data {
	u16 seq_number; /* agn only */
	u16 tfds_in_queue;
	struct iwl_ht_agg agg;
};

/**
 * struct iwl_shared - shared fields for all the layers of the driver
 *
 * @dbg_level_dev: dbg level set per device. Prevails on
 *	iwlagn_mod_params.debug_level if set (!= 0)
 * @ucode_owner: IWL_OWNERSHIP_*
 * @cmd_queue: command queue number
 * @status: STATUS_*
 * @bus: pointer to the bus layer data
 * @priv: pointer to the upper layer data
 * @hw_params: see struct iwl_hw_params
 * @workqueue: the workqueue used by all the layers of the driver
 * @lock: protect general shared data
 * @sta_lock: protects the station table.
 *	If lock and sta_lock are needed, lock must be acquired first.
 * @mutex:
 */
struct iwl_shared {
#ifdef CONFIG_IWLWIFI_DEBUG
	u32 dbg_level_dev;
#endif /* CONFIG_IWLWIFI_DEBUG */

#define IWL_OWNERSHIP_DRIVER	0
#define IWL_OWNERSHIP_TM	1
	u8 ucode_owner;
	u8 cmd_queue;
	unsigned long status;
	bool wowlan;

	struct iwl_bus *bus;
	struct iwl_priv *priv;
	struct iwl_trans *trans;
	struct iwl_hw_params hw_params;

	struct workqueue_struct *workqueue;
	spinlock_t lock;
	spinlock_t sta_lock;
	struct mutex mutex;

	struct iwl_tid_data tid_data[IWLAGN_STATION_COUNT][IWL_MAX_TID_COUNT];
};

/*Whatever _m is (iwl_trans, iwl_priv, iwl_bus, these macros will work */
#define priv(_m)	((_m)->shrd->priv)
#define bus(_m)		((_m)->shrd->bus)
#define trans(_m)	((_m)->shrd->trans)
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

static inline void iwl_free_pages(struct iwl_shared *shrd, unsigned long page)
{
	free_pages(page, shrd->hw_params.rx_page_order);
}

struct iwl_rx_mem_buffer {
	dma_addr_t page_dma;
	struct page *page;
	struct list_head list;
};

#define rxb_addr(r) page_address(r->page)

/*
 * mac80211 queues, ACs, hardware queues, FIFOs.
 *
 * Cf. http://wireless.kernel.org/en/developers/Documentation/mac80211/queues
 *
 * Mac80211 uses the following numbers, which we get as from it
 * by way of skb_get_queue_mapping(skb):
 *
 *	VO	0
 *	VI	1
 *	BE	2
 *	BK	3
 *
 *
 * Regular (not A-MPDU) frames are put into hardware queues corresponding
 * to the FIFOs, see comments in iwl-prph.h. Aggregated frames get their
 * own queue per aggregation session (RA/TID combination), such queues are
 * set up to map into FIFOs too, for which we need an AC->FIFO mapping. In
 * order to map frames to the right queue, we also need an AC->hw queue
 * mapping. This is implemented here.
 *
 * Due to the way hw queues are set up (by the hw specific modules like
 * iwl-4965.c, iwl-5000.c etc.), the AC->hw queue mapping is the identity
 * mapping.
 */

static const u8 tid_to_ac[] = {
	IEEE80211_AC_BE,
	IEEE80211_AC_BK,
	IEEE80211_AC_BK,
	IEEE80211_AC_BE,
	IEEE80211_AC_VI,
	IEEE80211_AC_VI,
	IEEE80211_AC_VO,
	IEEE80211_AC_VO
};

static inline int get_ac_from_tid(u16 tid)
{
	if (likely(tid < ARRAY_SIZE(tid_to_ac)))
		return tid_to_ac[tid];

	/* no support for TIDs 8-15 yet */
	return -EINVAL;
}

enum iwl_rxon_context_id {
	IWL_RXON_CTX_BSS,
	IWL_RXON_CTX_PAN,

	NUM_IWL_RXON_CTX
};

#ifdef CONFIG_PM
int iwl_suspend(struct iwl_priv *priv);
int iwl_resume(struct iwl_priv *priv);
#endif /* !CONFIG_PM */

int iwl_probe(struct iwl_bus *bus, const struct iwl_trans_ops *trans_ops,
		struct iwl_cfg *cfg);
void __devexit iwl_remove(struct iwl_priv * priv);

void iwl_start_tx_ba_trans_ready(struct iwl_priv *priv, u8 ctx,
				 u8 sta_id, u8 tid);

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

static inline int iwl_is_ready(struct iwl_shared *shrd)
{
	/* The adapter is 'ready' if READY and GEO_CONFIGURED bits are
	 * set but EXIT_PENDING is not */
	return test_bit(STATUS_READY, &shrd->status) &&
	       test_bit(STATUS_GEO_CONFIGURED, &shrd->status) &&
	       !test_bit(STATUS_EXIT_PENDING, &shrd->status);
}

static inline int iwl_is_alive(struct iwl_shared *shrd)
{
	return test_bit(STATUS_ALIVE, &shrd->status);
}

static inline int iwl_is_init(struct iwl_shared *shrd)
{
	return test_bit(STATUS_INIT, &shrd->status);
}

static inline int iwl_is_rfkill_hw(struct iwl_shared *shrd)
{
	return test_bit(STATUS_RF_KILL_HW, &shrd->status);
}

static inline int iwl_is_rfkill(struct iwl_shared *shrd)
{
	return iwl_is_rfkill_hw(shrd);
}

static inline int iwl_is_ctkill(struct iwl_shared *shrd)
{
	return test_bit(STATUS_CT_KILL, &shrd->status);
}

static inline int iwl_is_ready_rf(struct iwl_shared *shrd)
{
	if (iwl_is_rfkill(shrd))
		return 0;

	return iwl_is_ready(shrd);
}

#endif /* #__iwl_shared_h__ */
