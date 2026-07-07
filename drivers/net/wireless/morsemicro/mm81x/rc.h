/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_RC_H_
#define _MM81X_RC_H_

#include <linux/list.h>
#include <linux/workqueue.h>
#include "core.h"
#include "mmrc.h"

struct mm81x_vif;

#define INIT_MAX_RATES_NUM 4

struct mm81x_rc {
	/* Serialise rate control queue manipulation and timer functions */
	spinlock_t lock;
	struct list_head stas;
	struct timer_list timer;
	struct work_struct work;
	struct mm81x *mors;
};

struct mm81x_rc_sta {
	struct mmrc_table *tb;
	struct list_head list;
	unsigned long last_update;
};

void mm81x_rc_init(struct mm81x *mors);
void mm81x_rc_deinit(struct mm81x *mors);
int mm81x_rc_sta_add(struct mm81x *mors, struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta);
void mm81x_rc_sta_remove(struct mm81x *mors, struct ieee80211_sta *sta);
void mm81x_rc_sta_fill_tx_rates(struct mm81x *mors,
				struct mm81x_skb_tx_info *tx_info,
				struct sk_buff *skb, struct ieee80211_sta *sta,
				int tx_bw, bool rts_allowed);
void mm81x_rc_sta_feedback_rates(struct mm81x *mors, struct sk_buff *skb,
				 struct ieee80211_sta *sta,
				 struct mm81x_skb_tx_status *tx_sts,
				 int tx_attempts);
void mm81x_rc_sta_state_check(struct mm81x *mors, struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      enum ieee80211_sta_state old_state,
			      enum ieee80211_sta_state new_state);

#endif /* !_MM81X_RC_H_ */
