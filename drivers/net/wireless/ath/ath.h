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

#include <linux/etherdevice.h>
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

enum ath_op_flags {
	ATH_OP_INVALID,
	ATH_OP_BEACONS,
	ATH_OP_ANI_RUN,
	ATH_OP_PRIM_STA_VIF,
	ATH_OP_HW_RESET,
	ATH_OP_SCANNING,
	ATH_OP_MULTI_CHANNEL,
	ATH_OP_WOW_ENABLED,
};

enum ath_bus_type {
	ATH_PCI,
	ATH_AHB,
	ATH_USB,
};

struct reg_dmn_pair_mapping {
	u16 reg_domain;
	u16 reg_5ghz_ctl;
	u16 reg_2ghz_ctl;
};

struct ath_regulatory {
	char alpha2[2];
	enum nl80211_dfs_regions region;
	u16 country_code;
	u16 max_power_level;
	u16 current_rd;
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

/**
 * struct ath_ops - Register read/write operations
 *
 * @read: Register read
 * @multi_read: Multiple register read
 * @write: Register write
 * @enable_write_buffer: Enable multiple register writes
 * @write_flush: flush buffered register writes and disable buffering
 */
struct ath_ops {
	unsigned int (*read)(void *, u32 reg_offset);
	void (*multi_read)(void *, u32 *addr, u32 *val, u16 count);
	void (*write)(void *, u32 val, u32 reg_offset);
	void (*enable_write_buffer)(void *);
	void (*write_flush) (void *);
	u32 (*rmw)(void *, u32 reg_offset, u32 set, u32 clr);
	void (*enable_rmw_buffer)(void *);
	void (*rmw_flush) (void *);

};

struct ath_common;
struct ath_bus_ops;

struct ath_ps_ops {
	void (*wakeup)(struct ath_common *common);
	void (*restore)(struct ath_common *common);
};

struct ath_common {
	void *ah;
	void *priv;
	struct ieee80211_hw *hw;
	int debug_mask;
	enum ath_device_state state;
	unsigned long op_flags;

	struct ath_ani ani;

	u16 cachelsz;
	u16 curaid;
	u8 macaddr[ETH_ALEN];
	u8 curbssid[ETH_ALEN] __aligned(2);
	u8 bssidmask[ETH_ALEN];

	u32 rx_bufsize;

	u32 keymax;
	DECLARE_BITMAP(keymap, ATH_KEYMAX);
	DECLARE_BITMAP(tkip_keymap, ATH_KEYMAX);
	DECLARE_BITMAP(ccmp_keymap, ATH_KEYMAX);
	enum ath_crypt_caps crypt_caps;

	unsigned int clockrate;

	spinlock_t cc_lock;
	struct ath_cycle_counters cc_ani;
	struct ath_cycle_counters cc_survey;

	struct ath_regulatory regulatory;
	struct ath_regulatory reg_world_copy;
	const struct ath_ops *ops;
	const struct ath_bus_ops *bus_ops;
	const struct ath_ps_ops *ps_ops;

	bool btcoex_enabled;
	bool disable_ani;
	bool bt_ant_diversity;

	int last_rssi;
	struct ieee80211_supported_band sbands[NUM_NL80211_BANDS];
};

static inline const struct ath_ps_ops *ath_ps_ops(struct ath_common *common)
{
	return common->ps_ops;
}

struct sk_buff *ath_rxbuf_alloc(struct ath_common *common,
				u32 len,
				gfp_t gfp_mask);
bool ath_is_mybeacon(struct ath_common *common, struct ieee80211_hdr *hdr);

void ath_hw_setbssidmask(struct ath_common *common);
void ath_key_delete(struct ath_common *common, struct ieee80211_key_conf *key);
int ath_key_config(struct ath_common *common,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key);
bool ath_hw_keyreset(struct ath_common *common, u16 entry);
bool ath_hw_keysetmac(struct ath_common *common, u16 entry, const u8 *mac);
void ath_hw_cycle_counters_update(struct ath_common *common);
int32_t ath_hw_get_listen_time(struct ath_common *common);

__printf(3, 4)
void ath_printk(const char *level, const struct ath_common *common,
		const char *fmt, ...);

#define ath_emerg(common, fmt, ...)				\
	ath_printk(KERN_EMERG, common, fmt, ##__VA_ARGS__)
#define ath_alert(common, fmt, ...)				\
	ath_printk(KERN_ALERT, common, fmt, ##__VA_ARGS__)
#define ath_crit(common, fmt, ...)				\
	ath_printk(KERN_CRIT, common, fmt, ##__VA_ARGS__)
#define ath_err(common, fmt, ...)				\
	ath_printk(KERN_ERR, common, fmt, ##__VA_ARGS__)
#define ath_warn(common, fmt, ...)				\
	ath_printk(KERN_WARNING, common, fmt, ##__VA_ARGS__)
#define ath_notice(common, fmt, ...)				\
	ath_printk(KERN_NOTICE, common, fmt, ##__VA_ARGS__)
#define ath_info(common, fmt, ...)				\
	ath_printk(KERN_INFO, common, fmt, ##__VA_ARGS__)

/**
 * enum ath_debug_level - atheros wireless debug level
 *
 * @ATH_DBG_RESET: reset processing
 * @ATH_DBG_QUEUE: hardware queue management
 * @ATH_DBG_EEPROM: eeprom processing
 * @ATH_DBG_CALIBRATE: periodic calibration
 * @ATH_DBG_INTERRUPT: interrupt processing
 * @ATH_DBG_REGULATORY: regulatory processing
 * @ATH_DBG_ANI: adaptive noise immunitive processing
 * @ATH_DBG_XMIT: basic xmit operation
 * @ATH_DBG_BEACON: beacon handling
 * @ATH_DBG_CONFIG: configuration of the hardware
 * @ATH_DBG_FATAL: fatal errors, this is the default, DBG_DEFAULT
 * @ATH_DBG_PS: power save processing
 * @ATH_DBG_HWTIMER: hardware timer handling
 * @ATH_DBG_BTCOEX: bluetooth coexistance
 * @ATH_DBG_BSTUCK: stuck beacons
 * @ATH_DBG_MCI: Message Coexistence Interface, a private protocol
 *	used exclusively for WLAN-BT coexistence starting from
 *	AR9462.
 * @ATH_DBG_DFS: radar datection
 * @ATH_DBG_WOW: Wake on Wireless
 * @ATH_DBG_DYNACK: dynack handling
 * @ATH_DBG_SPECTRAL_SCAN: FFT spectral scan
 * @ATH_DBG_ANY: enable all debugging
 *
 * The debug level is used to control the amount and type of debugging output
 * we want to see. Each driver has its own method for enabling debugging and
 * modifying debug level states -- but this is typically done through a
 * module parameter 'debug' along with a respective 'debug' debugfs file
 * entry.
 */
enum ATH_DEBUG {
	ATH_DBG_RESET		= 0x00000001,
	ATH_DBG_QUEUE		= 0x00000002,
	ATH_DBG_EEPROM		= 0x00000004,
	ATH_DBG_CALIBRATE	= 0x00000008,
	ATH_DBG_INTERRUPT	= 0x00000010,
	ATH_DBG_REGULATORY	= 0x00000020,
	ATH_DBG_ANI		= 0x00000040,
	ATH_DBG_XMIT		= 0x00000080,
	ATH_DBG_BEACON		= 0x00000100,
	ATH_DBG_CONFIG		= 0x00000200,
	ATH_DBG_FATAL		= 0x00000400,
	ATH_DBG_PS		= 0x00000800,
	ATH_DBG_BTCOEX		= 0x00001000,
	ATH_DBG_WMI		= 0x00002000,
	ATH_DBG_BSTUCK		= 0x00004000,
	ATH_DBG_MCI		= 0x00008000,
	ATH_DBG_DFS		= 0x00010000,
	ATH_DBG_WOW		= 0x00020000,
	ATH_DBG_CHAN_CTX	= 0x00040000,
	ATH_DBG_DYNACK		= 0x00080000,
	ATH_DBG_SPECTRAL_SCAN	= 0x00100000,
	ATH_DBG_ANY		= 0xffffffff
};

#define ATH_DBG_DEFAULT (ATH_DBG_FATAL)
#define ATH_DBG_MAX_LEN 512

#ifdef CONFIG_ATH_DEBUG

#define ath_dbg(common, dbg_mask, fmt, ...)				\
do {									\
	if ((common)->debug_mask & ATH_DBG_##dbg_mask)			\
		ath_printk(KERN_DEBUG, common, fmt, ##__VA_ARGS__);	\
} while (0)

#define ATH_DBG_WARN(foo, arg...) WARN(foo, arg)
#define ATH_DBG_WARN_ON_ONCE(foo) WARN_ON_ONCE(foo)

#else

static inline  __attribute__ ((format (printf, 3, 4)))
void _ath_dbg(struct ath_common *common, enum ATH_DEBUG dbg_mask,
	     const char *fmt, ...)
{
}
#define ath_dbg(common, dbg_mask, fmt, ...)				\
	_ath_dbg(common, ATH_DBG_##dbg_mask, fmt, ##__VA_ARGS__)

#define ATH_DBG_WARN(foo, arg...) do {} while (0)
#define ATH_DBG_WARN_ON_ONCE(foo) ({				\
	int __ret_warn_once = !!(foo);				\
	unlikely(__ret_warn_once);				\
})

#endif /* CONFIG_ATH_DEBUG */

/** Returns string describing opmode, or NULL if unknown mode. */
#ifdef CONFIG_ATH_DEBUG
const char *ath_opmode_to_string(enum nl80211_iftype opmode);
#else
static inline const char *ath_opmode_to_string(enum nl80211_iftype opmode)
{
	return "UNKNOWN";
}
#endif

extern const char *ath_bus_type_strings[];
static inline const char *ath_bus_type_to_string(enum ath_bus_type bustype)
{
	return ath_bus_type_strings[bustype];
}

#endif /* ATH_H */
