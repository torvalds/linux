/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef ATH_H
#define ATH_H

#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/spinlock.h>
#include <net/mac80211.h>

/*
 * The key cache is used for h/w cipher state and also for
 * tracking station state such as the current tx antenna.
 * We also setup a mapping table between key cache slot indices
 * and station state to short-circuit node lookups on rx.
 * Different parts have different size key caches.  We handle
 * up to ATH_KEYMAX entries (could dynamically allocate state).
 */
#define	ATH_KEYMAX	        128     /* max key cache size we handle */

static const u8 ath_bcast_mac[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct ath_ani {
	bool caldone;
	unsigned int longcal_timer;
	unsigned int shortcal_timer;
	unsigned int resetcal_timer;
	unsigned int checkani_timer;
	struct timer_list timer;
};

struct ath_cycle_counters {
	u32 cycles;
	u32 rx_busy;
	u32 rx_frame;
	u32 tx_frame;
};

enum ath_device_state {
	ATH_HW_UNAVAILABLE,
	ATH_HW_INITIALIZED,
};

enum ath_bus_type {
	ATH_PCI,
	ATH_AHB,
	ATH_USB,
};

struct reg_dmn_pair_mapping {
	u16 regDmnEnum;
	u16 reg_5ghz_ctl;
	u16 reg_2ghz_ctl;
};

struct ath_regulatory {
	char alpha2[2];
	u16 country_code;
	u16 max_power_level;
	u32 tp_scale;
	u16 current_rd;
	u16 current_rd_ext;
	int16_t power_limit;
	struct reg_dmn_pair_mapping *regpair;
};

enum ath_crypt_caps {
	ATH_CRYPT_CAP_CIPHER_AESCCM		= BIT(0),
	ATH_CRYPT_CAP_MIC_COMBINED		= BIT(1),
};

struct ath_keyval {
	u8 kv_type;
	u8 kv_pad;
	u16 kv_len;
	u8 kv_val[16]; /* TK */
	u8 kv_mic[8]; /* Michael MIC key */
	u8 kv_txmic[8]; /* Michael MIC TX key (used only if the hardware
			 * supports both MIC keys in the same key cache entry;
			 * in that case, kv_mic is the RX key) */
};

enum ath_cipher {
	ATH_CIPHER_WEP = 0,
	ATH_CIPHER_AES_OCB = 1,
	ATH_CIPHER_AES_CCM = 2,
	ATH_CIPHER_CKIP = 3,
	ATH_CIPHER_TKIP = 4,
	ATH_CIPHER_CLR = 5,
	ATH_CIPHER_MIC = 127
};

enum ath_drv_info {
	AR7010_DEVICE		= BIT(0),
	AR9287_DEVICE		= BIT(1),
};

/**
 * struct ath_ops - Register read/write operations
 *
 * @read: Register read
 * @write: Register write
 * @enable_write_buffer: Enable multiple register writes
 * @write_flush: flush buffered register writes and disable buffering
 */
struct ath_ops {
	unsigned int (*read)(void *, u32 reg_offset);
	void (*write)(void *, u32 val, u32 reg_offset);
	void (*enable_write_buffer)(void *);
	void (*write_flush) (void *);
};

struct ath_common;

struct ath_bus_ops {
	enum ath_bus_type ath_bus_type;
	void (*read_cachesize)(struct ath_common *common, int *csz);
	bool (*eeprom_read)(struct ath_common *common, u32 off, u16 *data);
	void (*bt_coex_prep)(struct ath_common *common);
};

struct ath_common {
	void *ah;
	void *priv;
	struct ieee80211_hw *hw;
	int debug_mask;
	enum ath_device_state state;

	struct ath_ani ani;

	u16 cachelsz;
	u16 curaid;
	u8 macaddr[ETH_ALEN];
	u8 curbssid[ETH_ALEN];
	u8 bssidmask[ETH_ALEN];

	u8 tx_chainmask;
	u8 rx_chainmask;

	u32 rx_bufsize;
	u32 driver_info;

	u32 keymax;
	DECLARE_BITMAP(keymap, ATH_KEYMAX);
	DECLARE_BITMAP(tkip_keymap, ATH_KEYMAX);
	enum ath_crypt_caps crypt_caps;

	unsigned int clockrate;

	spinlock_t cc_lock;
	struct ath_cycle_counters cc_ani;
	struct ath_cycle_counters cc_survey;

	struct ath_regulatory regulatory;
	const struct ath_ops *ops;
	const struct ath_bus_ops *bus_ops;

	bool btcoex_enabled;
};

struct sk_buff *ath_rxbuf_alloc(struct ath_common *common,
				u32 len,
				gfp_t gfp_mask);

void ath_hw_setbssidmask(struct ath_common *common);
void ath_key_delete(struct ath_common *common, struct ieee80211_key_conf *key);
int ath_key_config(struct ath_common *common,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key);
bool ath_hw_keyreset(struct ath_common *common, u16 entry);
void ath_hw_cycle_counters_update(struct ath_common *common);
int32_t ath_hw_get_listen_time(struct ath_common *common);

#endif /* ATH_H */
