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
#include <linux/mm.h> /* for page_address */
#include <net/mac80211.h>

#include "iwl-commands.h"

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

struct iwl_bus;
struct iwl_priv;
struct iwl_trans;
struct iwl_sensitivity_ranges;
struct iwl_trans_ops;

#define DRV_NAME        "iwlwifi"
#define IWLWIFI_VERSION "in-tree:"
#define DRV_COPYRIGHT	"Copyright(c) 2003-2011 Intel Corporation"
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
 * @num_of_queues: number of tx queue, HW dependent
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
	int num_of_queues;
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
 * @max_txq_num: Max # Tx queues supported
 * @num_ampdu_queues: num of ampdu queues
 * @tx_chains_num: Number of TX chains
 * @rx_chains_num: Number of RX chains
 * @valid_tx_ant: usable antennas for TX
 * @valid_rx_ant: usable antennas for RX
 * @ht40_channel: is 40MHz width possible: BIT(IEEE80211_BAND_XXX)
 * @sku: sku read from EEPROM
 * @rx_page_order: Rx buffer page order
 * @max_inst_size: for ucode use
 * @max_data_size: for ucode use
 * @ct_kill_threshold: temperature threshold - in hw dependent unit
 * @ct_kill_exit_threshold: when to reeable the device - in hw dependent unit
 *	relevant for 1000, 6000 and up
 * @wd_timeout: TX queues watchdog timeout
 * @struct iwl_sensitivity_ranges: range of sensitivity values
 */
struct iwl_hw_params {
	u8  max_txq_num;
	u8  num_ampdu_queues;
	u8  tx_chains_num;
	u8  rx_chains_num;
	u8  valid_tx_ant;
	u8  valid_rx_ant;
	u8  ht40_channel;
	bool shadow_reg_enable;
	u16 sku;
	u32 rx_page_order;
	u32 max_inst_size;
	u32 max_data_size;
	u32 ct_kill_threshold;
	u32 ct_kill_exit_threshold;
	unsigned int wd_timeout;

	const struct iwl_sensitivity_ranges *sens;
};

/**
 * enum iwl_ucode_type
 *
 * The type of ucode currently loaded on the hardware.
 *
 * @IWL_UCODE_NONE: No ucode loaded
 * @IWL_UCODE_REGULAR: Normal runtime ucode
 * @IWL_UCODE_INIT: Initial ucode
 * @IWL_UCODE_WOWLAN: Wake on Wireless enabled ucode
 */
enum iwl_ucode_type {
	IWL_UCODE_NONE,
	IWL_UCODE_REGULAR,
	IWL_UCODE_INIT,
	IWL_UCODE_WOWLAN,
};

/**
 * struct iwl_notification_wait - notification wait entry
 * @list: list head for global list
 * @fn: function called with the notification
 * @cmd: command ID
 *
 * This structure is not used directly, to wait for a
 * notification declare it on the stack, and call
 * iwlagn_init_notification_wait() with appropriate
 * parameters. Then do whatever will cause the ucode
 * to notify the driver, and to wait for that then
 * call iwlagn_wait_notification().
 *
 * Each notification is one-shot. If at some point we
 * need to support multi-shot notifications (which
 * can't be allocated on the stack) we need to modify
 * the code for them.
 */
struct iwl_notification_wait {
	struct list_head list;

	void (*fn)(struct iwl_trans *trans, struct iwl_rx_packet *pkt,
		   void *data);
	void *fn_data;

	u8 cmd;
	bool triggered, aborted;
};

/**
 * enum iwl_pa_type - Power Amplifier type
 * @IWL_PA_SYSTEM:  based on uCode configuration
 * @IWL_PA_INTERNAL: use Internal only
 */
enum iwl_pa_type {
	IWL_PA_SYSTEM = 0,
	IWL_PA_INTERNAL = 1,
};

/*
 * LED mode
 *    IWL_LED_DEFAULT:  use device default
 *    IWL_LED_RF_STATE: turn LED on/off based on RF state
 *			LED ON  = RF ON
 *			LED OFF = RF OFF
 *    IWL_LED_BLINK:    adjust led blink rate based on blink table
 */
enum iwl_led_mode {
	IWL_LED_DEFAULT,
	IWL_LED_RF_STATE,
	IWL_LED_BLINK,
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
 * @valid_tx_ant: valid transmit antenna
 * @valid_rx_ant: valid receive antenna
 * @sku: sku information from EEPROM
 * @eeprom_ver: EEPROM version
 * @eeprom_calib_ver: EEPROM calibration version
 * @lib: pointer to the lib ops
 * @additional_nic_config: additional nic configuration
 * @base_params: pointer to basic parameters
 * @ht_params: point to ht patameters
 * @bt_params: pointer to bt parameters
 * @pa_type: used by 6000 series only to identify the type of Power Amplifier
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
	u8   valid_tx_ant;
	u8   valid_rx_ant;
	u16  sku;
	u16  eeprom_ver;
	u16  eeprom_calib_ver;
	const struct iwl_lib_ops *lib;
	void (*additional_nic_config)(struct iwl_priv *priv);
	/* params not likely to change within a device family */
	struct iwl_base_params *base_params;
	/* params likely to change within a device family */
	struct iwl_ht_params *ht_params;
	struct iwl_bt_params *bt_params;
	enum iwl_pa_type pa_type;	  /* if used set to IWL_PA_SYSTEM */
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
 * @dbg_level_dev: dbg level set per device. Prevails on
 *	iwlagn_mod_params.debug_level if set (!= 0)
 * @ucode_owner: IWL_OWNERSHIP_*
 * @cmd_queue: command queue number
 * @status: STATUS_*
 * @wowlan: are we running wowlan uCode
 * @valid_contexts: microcode/device supports multiple contexts
 * @bus: pointer to the bus layer data
 * @cfg: see struct iwl_cfg
 * @priv: pointer to the upper layer data
 * @trans: pointer to the transport layer data
 * @hw_params: see struct iwl_hw_params
 * @workqueue: the workqueue used by all the layers of the driver
 * @lock: protect general shared data
 * @sta_lock: protects the station table.
 *	If lock and sta_lock are needed, lock must be acquired first.
 * @mutex:
 * @wait_command_queue: the wait_queue for SYNC host command nad uCode load
 * @eeprom: pointer to the eeprom/OTP image
 * @ucode_type: indicator of loaded ucode image
 * @notif_waits: things waiting for notification
 * @notif_wait_lock: lock protecting notification
 * @notif_waitq: head of notification wait queue
 * @device_pointers: pointers to ucode event tables
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
	u8 valid_contexts;

	struct iwl_bus *bus;
	struct iwl_cfg *cfg;
	struct iwl_priv *priv;
	struct iwl_trans *trans;
	struct iwl_hw_params hw_params;

	struct workqueue_struct *workqueue;
	spinlock_t lock;
	spinlock_t sta_lock;
	struct mutex mutex;

	wait_queue_head_t wait_command_queue;

	/* eeprom -- this is in the card's little endian byte order */
	u8 *eeprom;

	/* ucode related variables */
	enum iwl_ucode_type ucode_type;

	/* notification wait support */
	struct list_head notif_waits;
	spinlock_t notif_wait_lock;
	wait_queue_head_t notif_waitq;

	struct {
		u32 error_event_table;
		u32 log_event_table;
	} device_pointers;

};

/*Whatever _m is (iwl_trans, iwl_priv, iwl_bus, these macros will work */
#define priv(_m)	((_m)->shrd->priv)
#define cfg(_m)		((_m)->shrd->cfg)
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

/**
 * iwl_queue_inc_wrap - increment queue index, wrap back to beginning
 * @index -- current index
 * @n_bd -- total number of entries in queue (must be power of 2)
 */
static inline int iwl_queue_inc_wrap(int index, int n_bd)
{
	return ++index & (n_bd - 1);
}

/**
 * iwl_queue_dec_wrap - decrement queue index, wrap back to end
 * @index -- current index
 * @n_bd -- total number of entries in queue (must be power of 2)
 */
static inline int iwl_queue_dec_wrap(int index, int n_bd)
{
	return --index & (n_bd - 1);
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

int iwl_probe(struct iwl_bus *bus, const struct iwl_trans_ops *trans_ops,
		struct iwl_cfg *cfg);
void __devexit iwl_remove(struct iwl_priv * priv);
struct iwl_device_cmd;
int __must_check iwl_rx_dispatch(struct iwl_priv *priv,
				 struct iwl_rx_mem_buffer *rxb,
				 struct iwl_device_cmd *cmd);

int iwlagn_hw_valid_rtc_data_addr(u32 addr);
void iwl_set_hw_rfkill_state(struct iwl_priv *priv, bool state);
void iwl_nic_config(struct iwl_priv *priv);
void iwl_free_skb(struct iwl_priv *priv, struct sk_buff *skb);
void iwl_apm_stop(struct iwl_priv *priv);
int iwl_apm_init(struct iwl_priv *priv);
void iwlagn_fw_error(struct iwl_priv *priv, bool ondemand);
const char *get_cmd_string(u8 cmd);
bool iwl_check_for_ct_kill(struct iwl_priv *priv);

void iwl_stop_sw_queue(struct iwl_priv *priv, u8 ac);
void iwl_wake_sw_queue(struct iwl_priv *priv, u8 ac);

/* notification wait support */
void iwl_abort_notification_waits(struct iwl_shared *shrd);
void __acquires(wait_entry)
iwl_init_notification_wait(struct iwl_shared *shrd,
			      struct iwl_notification_wait *wait_entry,
			      u8 cmd,
			      void (*fn)(struct iwl_trans *trans,
					 struct iwl_rx_packet *pkt,
					 void *data),
			      void *fn_data);
int __must_check __releases(wait_entry)
iwl_wait_notification(struct iwl_shared *shrd,
			 struct iwl_notification_wait *wait_entry,
			 unsigned long timeout);
void __releases(wait_entry)
iwl_remove_notification(struct iwl_shared *shrd,
			   struct iwl_notification_wait *wait_entry);

#ifdef CONFIG_IWLWIFI_DEBUGFS
void iwl_reset_traffic_log(struct iwl_priv *priv);
#endif /* CONFIG_IWLWIFI_DEBUGFS */

#ifdef CONFIG_IWLWIFI_DEBUG
void iwl_print_rx_config_cmd(struct iwl_priv *priv,
			     enum iwl_rxon_context_id ctxid);
#else
static inline void iwl_print_rx_config_cmd(struct iwl_priv *priv,
					   enum iwl_rxon_context_id ctxid)
{
}
#endif

#define IWL_CMD(x) case x: return #x
#define IWL_MASK(lo, hi) ((1 << (hi)) | ((1 << (hi)) - (1 << (lo))))

#define IWL_TRAFFIC_ENTRIES	(256)
#define IWL_TRAFFIC_ENTRY_SIZE  (64)

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
