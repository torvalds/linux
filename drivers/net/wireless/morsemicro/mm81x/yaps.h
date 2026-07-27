/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_YAPS_H_
#define _MM81X_YAPS_H_

#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include "skbq.h"

#define YAPS_TX_SKBQ_MAX 4

struct mm81x_hif_ops;
extern const struct mm81x_hif_ops mm81x_yaps_ops;

enum mm81x_yaps_to_chip_q {
	MM81X_YAPS_TX_Q = 0,
	MM81X_YAPS_CMD_Q,
	MM81X_YAPS_BEACON_Q,
	MM81X_YAPS_MGMT_Q,
	/* Keep this last */
	MM81X_YAPS_NUM_TC_Q
};

struct mm81x_yaps_pkt {
	struct sk_buff *skb;
	enum mm81x_yaps_to_chip_q tc_queue;
};

struct mm81x_yaps {
	struct mm81x *mors;
	struct mm81x_yaps_hw_aux_data *aux_data;
	const struct mm81x_yaps_ops *ops;
	u8 flags;
	struct {
		struct mm81x_yaps_pkt *to_chip_pkts;
		struct mm81x_yaps_pkt *from_chip_pkts;
	} hw;

	/* Chip interface is stopping, new work should not be enqueued. */
	bool finish;

	struct mm81x_skbq data_tx_qs[YAPS_TX_SKBQ_MAX];
	struct mm81x_skbq beacon_q;
	struct mm81x_skbq mgmt_q;
	struct mm81x_skbq data_rx_q;
	struct mm81x_skbq cmd_q;
	struct mm81x_skbq cmd_resp_q;

	struct {
		struct timer_list timer;
		unsigned long retry_expiry;
		bool is_full;
	} chip_queue_full;
};

struct mm81x_yaps_ops {
	int (*write_pkts)(struct mm81x_yaps *yaps, struct mm81x_yaps_pkt *pkts,
			  int num_pkts, int *num_pkts_sent);
	int (*read_pkts)(struct mm81x_yaps *yaps, struct mm81x_yaps_pkt *pkts,
			 int num_pkts_max, int *num_pkts_received);
	int (*update_status)(struct mm81x_yaps *yaps);
};

int mm81x_yaps_init(struct mm81x *mors);
void mm81x_yaps_show(struct mm81x_yaps *yaps, struct seq_file *file);
void mm81x_yaps_finish(struct mm81x *mors);
void mm81x_yaps_flush_tx_data(struct mm81x *mors);
void mm81x_yaps_flush_cmds(struct mm81x *mors);
void mm81x_yaps_work(struct work_struct *work);
void mm81x_yaps_stale_tx_work(struct work_struct *work);
int mm81x_yaps_get_tx_status_pending_count(struct mm81x *mors);
int mm81x_yaps_get_tx_buffered_count(struct mm81x *mors);

#endif /* !_MM81X_YAPS_H_ */
