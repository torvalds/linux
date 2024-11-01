/*
 * Copyright (c) 2014, Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
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

#ifndef DYNACK_H
#define DYNACK_H

#define ATH_DYN_BUF	64

struct ath_hw;
struct ath_node;

/**
 * struct ath_dyn_rxbuf - ACK frame ring buffer
 * @h_rb: ring buffer head
 * @t_rb: ring buffer tail
 * @tstamp: ACK RX timestamp buffer
 */
struct ath_dyn_rxbuf {
	u16 h_rb, t_rb;
	u32 tstamp[ATH_DYN_BUF];
};

struct ts_info {
	u32 tstamp;
	u32 dur;
};

struct haddr_pair {
	u8 h_dest[ETH_ALEN];
	u8 h_src[ETH_ALEN];
};

/**
 * struct ath_dyn_txbuf - tx frame ring buffer
 * @h_rb: ring buffer head
 * @t_rb: ring buffer tail
 * @addr: dest/src address pair for a given TX frame
 * @ts: TX frame timestamp buffer
 */
struct ath_dyn_txbuf {
	u16 h_rb, t_rb;
	struct haddr_pair addr[ATH_DYN_BUF];
	struct ts_info ts[ATH_DYN_BUF];
};

/**
 * struct ath_dynack - dynack processing info
 * @enabled: enable dyn ack processing
 * @ackto: current ACK timeout
 * @lto: last ACK timeout computation
 * @nodes: ath_node linked list
 * @qlock: ts queue spinlock
 * @ack_rbf: ACK ts ring buffer
 * @st_rbf: status ts ring buffer
 */
struct ath_dynack {
	bool enabled;
	int ackto;
	unsigned long lto;

	struct list_head nodes;

	/* protect timestamp queue access */
	spinlock_t qlock;
	struct ath_dyn_rxbuf ack_rbf;
	struct ath_dyn_txbuf st_rbf;
};

#if defined(CONFIG_ATH9K_DYNACK)
void ath_dynack_reset(struct ath_hw *ah);
void ath_dynack_node_init(struct ath_hw *ah, struct ath_node *an);
void ath_dynack_node_deinit(struct ath_hw *ah, struct ath_node *an);
void ath_dynack_init(struct ath_hw *ah);
void ath_dynack_sample_ack_ts(struct ath_hw *ah, struct sk_buff *skb, u32 ts);
void ath_dynack_sample_tx_ts(struct ath_hw *ah, struct sk_buff *skb,
			     struct ath_tx_status *ts,
			     struct ieee80211_sta *sta);
#else
static inline void ath_dynack_init(struct ath_hw *ah) {}
static inline void ath_dynack_node_init(struct ath_hw *ah,
					struct ath_node *an) {}
static inline void ath_dynack_node_deinit(struct ath_hw *ah,
					  struct ath_node *an) {}
static inline void ath_dynack_sample_ack_ts(struct ath_hw *ah,
					    struct sk_buff *skb, u32 ts) {}
static inline void ath_dynack_sample_tx_ts(struct ath_hw *ah,
					   struct sk_buff *skb,
					   struct ath_tx_status *ts,
					   struct ieee80211_sta *sta) {}
#endif

#endif /* DYNACK_H */
