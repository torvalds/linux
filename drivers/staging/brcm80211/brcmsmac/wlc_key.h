/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _wlc_key_h_
#define _wlc_key_h_

struct scb;
struct wlc_info;
struct wlc_bsscfg;
/* Maximum # of keys that wl driver supports in S/W.
 * Keys supported in H/W is less than or equal to WSEC_MAX_KEYS.
 */
#define WSEC_MAX_KEYS		54	/* Max # of keys (50 + 4 default keys) */
#define WLC_DEFAULT_KEYS	4	/* Default # of keys */

#define WSEC_MAX_WOWL_KEYS 5	/* Max keys in WOWL mode (1 + 4 default keys) */

#define WPA2_GTK_MAX	3

/*
* Max # of keys currently supported:
*
*     s/w keys if WSEC_SW(wlc->wsec).
*     h/w keys otherwise.
*/
#define WLC_MAX_WSEC_KEYS(wlc) WSEC_MAX_KEYS

/* number of 802.11 default (non-paired, group keys) */
#define WSEC_MAX_DEFAULT_KEYS	4	/* # of default keys */

/* Max # of hardware keys supported */
#define WLC_MAX_WSEC_HW_KEYS(wlc) WSEC_MAX_RCMTA_KEYS

/* Max # of hardware TKIP MIC keys supported */
#define WLC_MAX_TKMIC_HW_KEYS(wlc) (WSEC_MAX_TKMIC_ENGINE_KEYS)

#define WSEC_HW_TKMIC_KEY(wlc, key, bsscfg) \
	((((wlc)->machwcap & MCAP_TKIPMIC)) && \
	 (key) && ((key)->algo == CRYPTO_ALGO_TKIP) && \
	 !WSEC_SOFTKEY(wlc, key, bsscfg) && \
	WSEC_KEY_INDEX(wlc, key) >= WLC_DEFAULT_KEYS && \
	(WSEC_KEY_INDEX(wlc, key) < WSEC_MAX_TKMIC_ENGINE_KEYS))

/* index of key in key table */
#define WSEC_KEY_INDEX(wlc, key)	((key)->idx)

#define WSEC_SOFTKEY(wlc, key, bsscfg) (WLC_SW_KEYS(wlc, bsscfg) || \
	WSEC_KEY_INDEX(wlc, key) >= WLC_MAX_WSEC_HW_KEYS(wlc))

/* get a key, non-NULL only if key allocated and not clear */
#define WSEC_KEY(wlc, i)	(((wlc)->wsec_keys[i] && (wlc)->wsec_keys[i]->len) ? \
	(wlc)->wsec_keys[i] : NULL)

#define WSEC_SCB_KEY_VALID(scb)	(((scb)->key && (scb)->key->len) ? true : false)

/* default key */
#define WSEC_BSS_DEFAULT_KEY(bsscfg) (((bsscfg)->wsec_index == -1) ? \
	(struct wsec_key *)NULL:(bsscfg)->bss_def_keys[(bsscfg)->wsec_index])

/* Macros for key management in IBSS mode */
#define WSEC_IBSS_MAX_PEERS	16	/* Max # of IBSS Peers */
#define WSEC_IBSS_RCMTA_INDEX(idx) \
	(((idx - WSEC_MAX_DEFAULT_KEYS) % WSEC_IBSS_MAX_PEERS) + WSEC_MAX_DEFAULT_KEYS)

/* contiguous # key slots for infrastructure mode STA */
#define WSEC_BSS_STA_KEY_GROUP_SIZE	5

typedef struct wsec_iv {
	u32 hi;		/* upper 32 bits of IV */
	u16 lo;		/* lower 16 bits of IV */
} wsec_iv_t;

#define WLC_NUMRXIVS	16	/* # rx IVs (one per 802.11e TID) */

typedef struct wsec_key {
	u8 ea[ETH_ALEN];	/* per station */
	u8 idx;		/* key index in wsec_keys array */
	u8 id;		/* key ID [0-3] */
	u8 algo;		/* CRYPTO_ALGO_AES_CCM, CRYPTO_ALGO_WEP128, etc */
	u8 rcmta;		/* rcmta entry index, same as idx by default */
	u16 flags;		/* misc flags */
	u8 algo_hw;		/* cache for hw register */
	u8 aes_mode;		/* cache for hw register */
	s8 iv_len;		/* IV length */
	s8 icv_len;		/* ICV length */
	u32 len;		/* key length..don't move this var */
	/* data is 4byte aligned */
	u8 data[WLAN_MAX_KEY_LEN];	/* key data */
	wsec_iv_t rxiv[WLC_NUMRXIVS];	/* Rx IV (one per TID) */
	wsec_iv_t txiv;		/* Tx IV */

} wsec_key_t;

#define broken_roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))

/* For use with wsec_key_t.flags */

#define WSEC_BS_UPDATE		(1 << 0)	/* Indicates hw needs key update on BS switch */
#define WSEC_PRIMARY_KEY	(1 << 1)	/* Indicates this key is the primary (ie tx) key */
#define WSEC_TKIP_ERROR		(1 << 2)	/* Provoke deliberate MIC error */
#define WSEC_REPLAY_ERROR	(1 << 3)	/* Provoke deliberate replay */
#define WSEC_IBSS_PEER_GROUP_KEY	(1 << 7)	/* Flag: group key for a IBSS PEER */
#define WSEC_ICV_ERROR		(1 << 8)	/* Provoke deliberate ICV error */

#define wlc_key_insert(a, b, c, d, e, f, g, h, i, j) (BCME_ERROR)
#define wlc_key_update(a, b, c) do {} while (0)
#define wlc_key_remove(a, b, c) do {} while (0)
#define wlc_key_remove_all(a, b) do {} while (0)
#define wlc_key_delete(a, b, c) do {} while (0)
#define wlc_scb_key_delete(a, b) do {} while (0)
#define wlc_key_lookup(a, b, c, d, e) (NULL)
#define wlc_key_hw_init_all(a) do {} while (0)
#define wlc_key_hw_init(a, b, c)  do {} while (0)
#define wlc_key_hw_wowl_init(a, b, c, d) do {} while (0)
#define wlc_key_sw_wowl_update(a, b, c, d, e) do {} while (0)
#define wlc_key_sw_wowl_create(a, b, c) (BCME_ERROR)
#define wlc_key_iv_update(a, b, c, d, e) do {(void)e; } while (0)
#define wlc_key_iv_init(a, b, c) do {} while (0)
#define wlc_key_set_error(a, b, c) (BCME_ERROR)
#define wlc_key_dump_hw(a, b) (BCME_ERROR)
#define wlc_key_dump_sw(a, b) (BCME_ERROR)
#define wlc_key_defkeyflag(a) (0)
#define wlc_rcmta_add_bssid(a, b) do {} while (0)
#define wlc_rcmta_del_bssid(a, b) do {} while (0)
#define wlc_key_scb_delete(a, b) do {} while (0)

#endif				/* _wlc_key_h_ */
