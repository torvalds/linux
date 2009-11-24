/*
 * Copyright (c) 2009 Atheros Communications Inc.
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

#include <net/mac80211.h>

#include "../ath.h"
#include "../debug.h"

#include "hw.h"

/* Common header for Atheros 802.11n base driver cores */

#define WME_NUM_TID             16
#define WME_BA_BMP_SIZE         64
#define WME_MAX_BA              WME_BA_BMP_SIZE
#define ATH_TID_MAX_BUFS        (2 * WME_MAX_BA)

#define WME_AC_BE   0
#define WME_AC_BK   1
#define WME_AC_VI   2
#define WME_AC_VO   3
#define WME_NUM_AC  4

#define ATH_RSSI_DUMMY_MARKER   0x127
#define ATH_RSSI_LPF_LEN 		10
#define RSSI_LPF_THRESHOLD		-20
#define ATH_RSSI_EP_MULTIPLIER     (1<<7)
#define ATH_EP_MUL(x, mul)         ((x) * (mul))
#define ATH_RSSI_IN(x)             (ATH_EP_MUL((x), ATH_RSSI_EP_MULTIPLIER))
#define ATH_LPF_RSSI(x, y, len) \
    ((x != ATH_RSSI_DUMMY_MARKER) ? (((x) * ((len) - 1) + (y)) / (len)) : (y))
#define ATH_RSSI_LPF(x, y) do {                     			\
    if ((y) >= RSSI_LPF_THRESHOLD)                         		\
	x = ATH_LPF_RSSI((x), ATH_RSSI_IN((y)), ATH_RSSI_LPF_LEN);  	\
} while (0)
#define ATH_EP_RND(x, mul) 						\
	((((x)%(mul)) >= ((mul)/2)) ? ((x) + ((mul) - 1)) / (mul) : (x)/(mul))

struct ath_atx_ac {
	int sched;
	int qnum;
	struct list_head list;
	struct list_head tid_q;
};

struct ath_buf_state {
	int bfs_nframes;
	u16 bfs_al;
	u16 bfs_frmlen;
	int bfs_seqno;
	int bfs_tidno;
	int bfs_retries;
	u8 bf_type;
	u32 bfs_keyix;
	enum ath9k_key_type bfs_keytype;
};

struct ath_buf {
	struct list_head list;
	struct ath_buf *bf_lastbf;	/* last buf of this unit (a frame or
					   an aggregate) */
	struct ath_buf *bf_next;	/* next subframe in the aggregate */
	struct sk_buff *bf_mpdu;	/* enclosing frame structure */
	struct ath_desc *bf_desc;	/* virtual addr of desc */
	dma_addr_t bf_daddr;		/* physical addr of desc */
	dma_addr_t bf_buf_addr;		/* physical addr of data buffer */
	bool bf_stale;
	bool bf_isnullfunc;
	u16 bf_flags;
	struct ath_buf_state bf_state;
	dma_addr_t bf_dmacontext;
	struct ath_wiphy *aphy;
};

struct ath_atx_tid {
	struct list_head list;
	struct list_head buf_q;
	struct ath_node *an;
	struct ath_atx_ac *ac;
	struct ath_buf *tx_buf[ATH_TID_MAX_BUFS];
	u16 seq_start;
	u16 seq_next;
	u16 baw_size;
	int tidno;
	int baw_head;   /* first un-acked tx buffer */
	int baw_tail;   /* next unused tx buffer slot */
	int sched;
	int paused;
	u8 state;
};

struct ath_node {
	struct ath_common *common;
	struct ath_atx_tid tid[WME_NUM_TID];
	struct ath_atx_ac ac[WME_NUM_AC];
	u16 maxampdu;
	u8 mpdudensity;
	int last_rssi;
};

int ath9k_cmn_rx_skb_preprocess(struct ath_common *common,
				struct ieee80211_hw *hw,
				struct sk_buff *skb,
				struct ath_rx_status *rx_stats,
				struct ieee80211_rx_status *rx_status,
				bool *decrypt_error);

void ath9k_cmn_rx_skb_postprocess(struct ath_common *common,
				  struct sk_buff *skb,
				  struct ath_rx_status *rx_stats,
				  struct ieee80211_rx_status *rxs,
				  bool decrypt_error);

int ath9k_cmn_padpos(__le16 frame_control);
