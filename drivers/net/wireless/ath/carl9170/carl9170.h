/*
 * Atheros CARL9170 driver
 *
 * Driver specific definitions
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2009, 2010, Christian Lamparter <chunkeey@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __CARL9170_H
#define __CARL9170_H

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <linux/usb.h>
#ifdef CONFIG_CARL9170_LEDS
#include <linux/leds.h>
#endif /* CONFIG_CARL170_LEDS */
#ifdef CONFIG_CARL9170_WPC
#include <linux/input.h>
#endif /* CONFIG_CARL9170_WPC */
#include "eeprom.h"
#include "wlan.h"
#include "hw.h"
#include "fwdesc.h"
#include "fwcmd.h"
#include "../regd.h"

#ifdef CONFIG_CARL9170_DEBUGFS
#include "debug.h"
#endif /* CONFIG_CARL9170_DEBUGFS */

#define CARL9170FW_NAME	"carl9170-1.fw"

#define PAYLOAD_MAX	(CARL9170_MAX_CMD_LEN / 4 - 1)

enum carl9170_rf_init_mode {
	CARL9170_RFI_NONE,
	CARL9170_RFI_WARM,
	CARL9170_RFI_COLD,
};

#define CARL9170_MAX_RX_BUFFER_SIZE		8192

enum carl9170_device_state {
	CARL9170_UNKNOWN_STATE,
	CARL9170_STOPPED,
	CARL9170_IDLE,
	CARL9170_STARTED,
};

#define CARL9170_NUM_TID		16
#define WME_BA_BMP_SIZE			64
#define CARL9170_TX_USER_RATE_TRIES	3

#define WME_AC_BE   2
#define WME_AC_BK   3
#define WME_AC_VI   1
#define WME_AC_VO   0

#define TID_TO_WME_AC(_tid)				\
	((((_tid) == 0) || ((_tid) == 3)) ? WME_AC_BE :	\
	 (((_tid) == 1) || ((_tid) == 2)) ? WME_AC_BK :	\
	 (((_tid) == 4) || ((_tid) == 5)) ? WME_AC_VI :	\
	 WME_AC_VO)

#define SEQ_DIFF(_start, _seq) \
	(((_start) - (_seq)) & 0x0fff)
#define SEQ_PREV(_seq) \
	(((_seq) - 1) & 0x0fff)
#define SEQ_NEXT(_seq) \
	(((_seq) + 1) & 0x0fff)
#define BAW_WITHIN(_start, _bawsz, _seqno) \
	((((_seqno) - (_start)) & 0xfff) < (_bawsz))

enum carl9170_tid_state {
	CARL9170_TID_STATE_INVALID,
	CARL9170_TID_STATE_KILLED,
	CARL9170_TID_STATE_SHUTDOWN,
	CARL9170_TID_STATE_SUSPEND,
	CARL9170_TID_STATE_PROGRESS,
	CARL9170_TID_STATE_IDLE,
	CARL9170_TID_STATE_XMIT,
};

#define CARL9170_BAW_BITS (2 * WME_BA_BMP_SIZE)
#define CARL9170_BAW_SIZE (BITS_TO_LONGS(CARL9170_BAW_BITS))
#define CARL9170_BAW_LEN (DIV_ROUND_UP(CARL9170_BAW_BITS, BITS_PER_BYTE))

struct carl9170_sta_tid {
	/* must be the first entry! */
	struct list_head list;

	/* temporary list for RCU unlink procedure */
	struct list_head tmp_list;

	/* lock for the following data structures */
	spinlock_t lock;

	unsigned int counter;
	enum carl9170_tid_state state;
	u8 tid;		/* TID number ( 0 - 15 ) */
	u16 max;	/* max. AMPDU size */

	u16 snx;	/* awaiting _next_ frame */
	u16 hsn;	/* highest _queued_ sequence */
	u16 bsn;	/* base of the tx/agg bitmap */
	unsigned long bitmap[CARL9170_BAW_SIZE];

	/* Preaggregation reorder queue */
	struct sk_buff_head queue;
};

#define CARL9170_QUEUE_TIMEOUT		256
#define CARL9170_BUMP_QUEUE		1000
#define CARL9170_TX_TIMEOUT		2500
#define CARL9170_JANITOR_DELAY		128
#define CARL9170_QUEUE_STUCK_TIMEOUT	5500

#define CARL9170_NUM_TX_AGG_MAX		30

/*
 * Tradeoff between stability/latency and speed.
 *
 * AR9170_TXQ_DEPTH is devised by dividing the amount of available
 * tx buffers with the size of a full ethernet frame + overhead.
 *
 * Naturally: The higher the limit, the faster the device CAN send.
 * However, even a slight over-commitment at the wrong time and the
 * hardware is doomed to send all already-queued frames at suboptimal
 * rates. This in turn leads to an enourmous amount of unsuccessful
 * retries => Latency goes up, whereas the throughput goes down. CRASH!
 */
#define CARL9170_NUM_TX_LIMIT_HARD	((AR9170_TXQ_DEPTH * 3) / 2)
#define CARL9170_NUM_TX_LIMIT_SOFT	(AR9170_TXQ_DEPTH)

struct carl9170_tx_queue_stats {
	unsigned int count;
	unsigned int limit;
	unsigned int len;
};

struct carl9170_vif {
	unsigned int id;
	struct ieee80211_vif *vif;
};

struct carl9170_vif_info {
	struct list_head list;
	bool active;
	unsigned int id;
	struct sk_buff *beacon;
	bool enable_beacon;
};

#define AR9170_NUM_RX_URBS	16
#define AR9170_NUM_RX_URBS_MUL	2
#define AR9170_NUM_TX_URBS	8
#define AR9170_NUM_RX_URBS_POOL (AR9170_NUM_RX_URBS_MUL * AR9170_NUM_RX_URBS)

enum carl9170_device_features {
	CARL9170_WPS_BUTTON		= BIT(0),
	CARL9170_ONE_LED		= BIT(1),
};

#ifdef CONFIG_CARL9170_LEDS
struct ar9170;

struct carl9170_led {
	struct ar9170 *ar;
	struct led_classdev l;
	char name[32];
	unsigned int toggled;
	bool last_state;
	bool registered;
};
#endif /* CONFIG_CARL9170_LEDS */

enum carl9170_restart_reasons {
	CARL9170_RR_NO_REASON = 0,
	CARL9170_RR_FATAL_FIRMWARE_ERROR,
	CARL9170_RR_TOO_MANY_FIRMWARE_ERRORS,
	CARL9170_RR_WATCHDOG,
	CARL9170_RR_STUCK_TX,
	CARL9170_RR_SLOW_SYSTEM,
	CARL9170_RR_COMMAND_TIMEOUT,
	CARL9170_RR_TOO_MANY_PHY_ERRORS,
	CARL9170_RR_LOST_RSP,
	CARL9170_RR_INVALID_RSP,
	CARL9170_RR_USER_REQUEST,

	__CARL9170_RR_LAST,
};

enum carl9170_erp_modes {
	CARL9170_ERP_INVALID,
	CARL9170_ERP_AUTO,
	CARL9170_ERP_MAC80211,
	CARL9170_ERP_OFF,
	CARL9170_ERP_CTS,
	CARL9170_ERP_RTS,
	__CARL9170_ERP_NUM,
};

struct ar9170 {
	struct ath_common common;
	struct ieee80211_hw *hw;
	struct mutex mutex;
	enum carl9170_device_state state;
	spinlock_t state_lock;
	enum carl9170_restart_reasons last_reason;
	bool registered;

	/* USB */
	struct usb_device *udev;
	struct usb_interface *intf;
	struct usb_anchor rx_anch;
	struct usb_anchor rx_work;
	struct usb_anchor rx_pool;
	struct usb_anchor tx_wait;
	struct usb_anchor tx_anch;
	struct usb_anchor tx_cmd;
	struct usb_anchor tx_err;
	struct tasklet_struct usb_tasklet;
	atomic_t tx_cmd_urbs;
	atomic_t tx_anch_urbs;
	atomic_t rx_anch_urbs;
	atomic_t rx_work_urbs;
	atomic_t rx_pool_urbs;
	kernel_ulong_t features;

	/* firmware settings */
	struct completion fw_load_wait;
	struct completion fw_boot_wait;
	struct {
		const struct carl9170fw_desc_head *desc;
		const struct firmware *fw;
		unsigned int offset;
		unsigned int address;
		unsigned int cmd_bufs;
		unsigned int api_version;
		unsigned int vif_num;
		unsigned int err_counter;
		unsigned int bug_counter;
		u32 beacon_addr;
		unsigned int beacon_max_len;
		bool rx_stream;
		bool tx_stream;
		bool rx_filter;
		unsigned int mem_blocks;
		unsigned int mem_block_size;
		unsigned int rx_size;
	} fw;

	/* reset / stuck frames/queue detection */
	struct work_struct restart_work;
	unsigned int restart_counter;
	unsigned long queue_stop_timeout[__AR9170_NUM_TXQ];
	unsigned long max_queue_stop_timeout[__AR9170_NUM_TXQ];
	bool needs_full_reset;
	atomic_t pending_restarts;

	/* interface mode settings */
	struct list_head vif_list;
	unsigned long vif_bitmap;
	unsigned int vifs;
	struct carl9170_vif vif_priv[AR9170_MAX_VIRTUAL_MAC];

	/* beaconing */
	spinlock_t beacon_lock;
	unsigned int global_pretbtt;
	unsigned int global_beacon_int;
	struct carl9170_vif_info *beacon_iter;
	unsigned int beacon_enabled;

	/* cryptographic engine */
	u64 usedkeys;
	bool rx_software_decryption;
	bool disable_offload;

	/* filter settings */
	u64 cur_mc_hash;
	u32 cur_filter;
	unsigned int filter_state;
	unsigned int rx_filter_caps;
	bool sniffer_enabled;

	/* MAC */
	enum carl9170_erp_modes erp_mode;

	/* PHY */
	struct ieee80211_channel *channel;
	int noise[4];
	unsigned int chan_fail;
	unsigned int total_chan_fail;
	u8 heavy_clip;
	u8 ht_settings;

	/* power calibration data */
	u8 power_5G_leg[4];
	u8 power_2G_cck[4];
	u8 power_2G_ofdm[4];
	u8 power_5G_ht20[8];
	u8 power_5G_ht40[8];
	u8 power_2G_ht20[8];
	u8 power_2G_ht40[8];

#ifdef CONFIG_CARL9170_LEDS
	/* LED */
	struct delayed_work led_work;
	struct carl9170_led leds[AR9170_NUM_LEDS];
#endif /* CONFIG_CARL9170_LEDS */

	/* qos queue settings */
	spinlock_t tx_stats_lock;
	struct carl9170_tx_queue_stats tx_stats[__AR9170_NUM_TXQ];
	struct ieee80211_tx_queue_params edcf[5];
	struct completion tx_flush;

	/* CMD */
	int cmd_seq;
	int readlen;
	u8 *readbuf;
	spinlock_t cmd_lock;
	struct completion cmd_wait;
	union {
		__le32 cmd_buf[PAYLOAD_MAX + 1];
		struct carl9170_cmd cmd;
		struct carl9170_rsp rsp;
	};

	/* statistics */
	unsigned int tx_dropped;
	unsigned int tx_ack_failures;
	unsigned int tx_fcs_errors;
	unsigned int rx_dropped;

	/* EEPROM */
	struct ar9170_eeprom eeprom;

	/* tx queuing */
	struct sk_buff_head tx_pending[__AR9170_NUM_TXQ];
	struct sk_buff_head tx_status[__AR9170_NUM_TXQ];
	struct delayed_work tx_janitor;
	unsigned long tx_janitor_last_run;
	bool tx_schedule;

	/* tx ampdu */
	struct work_struct ampdu_work;
	spinlock_t tx_ampdu_list_lock;
	struct carl9170_sta_tid *tx_ampdu_iter;
	struct list_head tx_ampdu_list;
	atomic_t tx_ampdu_upload;
	atomic_t tx_ampdu_scheduler;
	atomic_t tx_total_pending;
	atomic_t tx_total_queued;
	unsigned int tx_ampdu_list_len;
	int current_density;
	int current_factor;
	bool tx_ampdu_schedule;

	/* internal memory management */
	spinlock_t mem_lock;
	unsigned long *mem_bitmap;
	atomic_t mem_free_blocks;
	atomic_t mem_allocs;

	/* rxstream mpdu merge */
	struct ar9170_rx_head rx_plcp;
	bool rx_has_plcp;
	struct sk_buff *rx_failover;
	int rx_failover_missing;

#ifdef CONFIG_CARL9170_WPC
	struct {
		bool pbc_state;
		struct input_dev *pbc;
		char name[32];
		char phys[32];
	} wps;
#endif /* CONFIG_CARL9170_WPC */

#ifdef CONFIG_CARL9170_DEBUGFS
	struct carl9170_debug debug;
	struct dentry *debug_dir;
#endif /* CONFIG_CARL9170_DEBUGFS */

	/* PSM */
	struct work_struct ps_work;
	struct {
		unsigned int dtim_counter;
		unsigned long last_beacon;
		unsigned long last_action;
		unsigned long last_slept;
		unsigned int sleep_ms;
		unsigned int off_override;
		bool state;
	} ps;
};

enum carl9170_ps_off_override_reasons {
	PS_OFF_VIF	= BIT(0),
	PS_OFF_BCN	= BIT(1),
	PS_OFF_5GHZ	= BIT(2),
};

struct carl9170_ba_stats {
	u8 ampdu_len;
	u8 ampdu_ack_len;
	bool clear;
};

struct carl9170_sta_info {
	bool ht_sta;
	unsigned int ampdu_max_len;
	struct carl9170_sta_tid *agg[CARL9170_NUM_TID];
	struct carl9170_ba_stats stats[CARL9170_NUM_TID];
};

struct carl9170_tx_info {
	unsigned long timeout;
	struct ar9170 *ar;
	struct kref ref;
};

#define CHK_DEV_STATE(a, s)	(((struct ar9170 *)a)->state >= (s))
#define IS_INITIALIZED(a)	(CHK_DEV_STATE(a, CARL9170_STOPPED))
#define IS_ACCEPTING_CMD(a)	(CHK_DEV_STATE(a, CARL9170_IDLE))
#define IS_STARTED(a)		(CHK_DEV_STATE(a, CARL9170_STARTED))

static inline void __carl9170_set_state(struct ar9170 *ar,
	enum carl9170_device_state newstate)
{
	ar->state = newstate;
}

static inline void carl9170_set_state(struct ar9170 *ar,
	enum carl9170_device_state newstate)
{
	unsigned long flags;

	spin_lock_irqsave(&ar->state_lock, flags);
	__carl9170_set_state(ar, newstate);
	spin_unlock_irqrestore(&ar->state_lock, flags);
}

static inline void carl9170_set_state_when(struct ar9170 *ar,
	enum carl9170_device_state min, enum carl9170_device_state newstate)
{
	unsigned long flags;

	spin_lock_irqsave(&ar->state_lock, flags);
	if (CHK_DEV_STATE(ar, min))
		__carl9170_set_state(ar, newstate);
	spin_unlock_irqrestore(&ar->state_lock, flags);
}

/* exported interface */
void *carl9170_alloc(size_t priv_size);
int carl9170_register(struct ar9170 *ar);
void carl9170_unregister(struct ar9170 *ar);
void carl9170_free(struct ar9170 *ar);
void carl9170_restart(struct ar9170 *ar, const enum carl9170_restart_reasons r);
void carl9170_ps_check(struct ar9170 *ar);

/* USB back-end */
int carl9170_usb_open(struct ar9170 *ar);
void carl9170_usb_stop(struct ar9170 *ar);
void carl9170_usb_tx(struct ar9170 *ar, struct sk_buff *skb);
void carl9170_usb_handle_tx_err(struct ar9170 *ar);
int carl9170_exec_cmd(struct ar9170 *ar, const enum carl9170_cmd_oids,
		      u32 plen, void *payload, u32 rlen, void *resp);
int __carl9170_exec_cmd(struct ar9170 *ar, struct carl9170_cmd *cmd,
			const bool free_buf);
int carl9170_usb_restart(struct ar9170 *ar);
void carl9170_usb_reset(struct ar9170 *ar);

/* MAC */
int carl9170_init_mac(struct ar9170 *ar);
int carl9170_set_qos(struct ar9170 *ar);
int carl9170_update_multicast(struct ar9170 *ar, const u64 mc_hast);
int carl9170_mod_virtual_mac(struct ar9170 *ar, const unsigned int id,
			     const u8 *mac);
int carl9170_set_operating_mode(struct ar9170 *ar);
int carl9170_set_beacon_timers(struct ar9170 *ar);
int carl9170_set_dyn_sifs_ack(struct ar9170 *ar);
int carl9170_set_rts_cts_rate(struct ar9170 *ar);
int carl9170_set_ampdu_settings(struct ar9170 *ar);
int carl9170_set_slot_time(struct ar9170 *ar);
int carl9170_set_mac_rates(struct ar9170 *ar);
int carl9170_set_hwretry_limit(struct ar9170 *ar, const u32 max_retry);
int carl9170_update_beacon(struct ar9170 *ar, const bool submit);
int carl9170_upload_key(struct ar9170 *ar, const u8 id, const u8 *mac,
	const u8 ktype, const u8 keyidx, const u8 *keydata, const int keylen);
int carl9170_disable_key(struct ar9170 *ar, const u8 id);

/* RX */
void carl9170_rx(struct ar9170 *ar, void *buf, unsigned int len);
void carl9170_handle_command_response(struct ar9170 *ar, void *buf, u32 len);

/* TX */
int carl9170_op_tx(struct ieee80211_hw *hw, struct sk_buff *skb);
void carl9170_tx_janitor(struct work_struct *work);
void carl9170_tx_process_status(struct ar9170 *ar,
				const struct carl9170_rsp *cmd);
void carl9170_tx_status(struct ar9170 *ar, struct sk_buff *skb,
			const bool success);
void carl9170_tx_callback(struct ar9170 *ar, struct sk_buff *skb);
void carl9170_tx_drop(struct ar9170 *ar, struct sk_buff *skb);
void carl9170_tx_scheduler(struct ar9170 *ar);
void carl9170_tx_get_skb(struct sk_buff *skb);
int carl9170_tx_put_skb(struct sk_buff *skb);

/* LEDs */
#ifdef CONFIG_CARL9170_LEDS
int carl9170_led_register(struct ar9170 *ar);
void carl9170_led_unregister(struct ar9170 *ar);
#endif /* CONFIG_CARL9170_LEDS */
int carl9170_led_init(struct ar9170 *ar);
int carl9170_led_set_state(struct ar9170 *ar, const u32 led_state);

/* PHY / RF */
int carl9170_set_channel(struct ar9170 *ar, struct ieee80211_channel *channel,
	enum nl80211_channel_type bw, enum carl9170_rf_init_mode rfi);
int carl9170_get_noisefloor(struct ar9170 *ar);

/* FW */
int carl9170_parse_firmware(struct ar9170 *ar);
int carl9170_fw_fix_eeprom(struct ar9170 *ar);

extern struct ieee80211_rate __carl9170_ratetable[];
extern int modparam_noht;

static inline struct ar9170 *carl9170_get_priv(struct carl9170_vif *carl_vif)
{
	return container_of(carl_vif, struct ar9170,
			    vif_priv[carl_vif->id]);
}

static inline struct ieee80211_hdr *carl9170_get_hdr(struct sk_buff *skb)
{
	return (void *)((struct _carl9170_tx_superframe *)
		skb->data)->frame_data;
}

static inline u16 get_seq_h(struct ieee80211_hdr *hdr)
{
	return le16_to_cpu(hdr->seq_ctrl) >> 4;
}

static inline u16 carl9170_get_seq(struct sk_buff *skb)
{
	return get_seq_h(carl9170_get_hdr(skb));
}

static inline u16 get_tid_h(struct ieee80211_hdr *hdr)
{
	return (ieee80211_get_qos_ctl(hdr))[0] & IEEE80211_QOS_CTL_TID_MASK;
}

static inline u16 carl9170_get_tid(struct sk_buff *skb)
{
	return get_tid_h(carl9170_get_hdr(skb));
}

static inline struct ieee80211_vif *
carl9170_get_vif(struct carl9170_vif_info *priv)
{
	return container_of((void *)priv, struct ieee80211_vif, drv_priv);
}

/* Protected by ar->mutex or RCU */
static inline struct ieee80211_vif *carl9170_get_main_vif(struct ar9170 *ar)
{
	struct carl9170_vif_info *cvif;

	list_for_each_entry_rcu(cvif, &ar->vif_list, list) {
		if (cvif->active)
			return carl9170_get_vif(cvif);
	}

	return NULL;
}

static inline bool is_main_vif(struct ar9170 *ar, struct ieee80211_vif *vif)
{
	bool ret;

	rcu_read_lock();
	ret = (carl9170_get_main_vif(ar) == vif);
	rcu_read_unlock();
	return ret;
}

#endif /* __CARL9170_H */
