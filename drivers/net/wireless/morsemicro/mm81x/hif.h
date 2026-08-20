/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_HIF_H_
#define _MM81X_HIF_H_

#include "core.h"

struct mm81x_skbq;

#define MM81X_HIF_BYPASS_TX_STATUS_IRQ_NUM (15)
#define MM81X_HIF_BYPASS_CMD_RESP_IRQ_NUM (29)
#define MM81X_HIF_IRQ_BYPASS_TX_STATUS_AVAILABLE \
	BIT(MM81X_HIF_BYPASS_TX_STATUS_IRQ_NUM)
#define MM81X_HIF_IRQ_BYPASS_CMD_RESP_AVAILABLE \
	BIT(MM81X_HIF_BYPASS_CMD_RESP_IRQ_NUM)

/* Hardware IF interrupt mask. We may use any interrupts in this range */
#define MM81X_HIF_IRQ_MASK_ALL                                       \
	(GENMASK(13, 0) | MM81X_HIF_IRQ_BYPASS_TX_STATUS_AVAILABLE | \
	 MM81X_HIF_IRQ_BYPASS_CMD_RESP_AVAILABLE)

enum mm81x_hif_flags {
	MM81X_HIF_FLAGS_DIR_TO_HOST = BIT(0),
	MM81X_HIF_FLAGS_DIR_TO_CHIP = BIT(1),
	MM81X_HIF_FLAGS_COMMAND = BIT(2),
	MM81X_HIF_FLAGS_BEACON = BIT(3),
	MM81X_HIF_FLAGS_DATA = BIT(4)
};

struct mm81x_hif_ops {
	int (*init)(struct mm81x *mors);
	void (*flush_tx_data)(struct mm81x *mors);
	void (*flush_cmds)(struct mm81x *mors);
	void (*finish)(struct mm81x *mors);
	void (*skbq_get_tx_qs)(struct mm81x *mors, struct mm81x_skbq **qs,
			       int *num_qs);
	struct mm81x_skbq *(*get_tx_cmd_queue)(struct mm81x *mors);
	struct mm81x_skbq *(*get_tx_beacon_queue)(struct mm81x *mors);
	struct mm81x_skbq *(*get_tx_mgmt_queue)(struct mm81x *mors);
	struct mm81x_skbq *(*get_tx_data_queue)(struct mm81x *mors, int aci);
	int (*handle_irq)(struct mm81x *mors, u32 status);
	int (*get_tx_buffered_count)(struct mm81x *mors);
	int (*get_tx_status_pending_count)(struct mm81x *mors);
};

static inline void mm81x_hif_clear_events(struct mm81x *mors)
{
	mors->hif.event_flags = 0;
}

static inline int mm81x_hif_init(struct mm81x *mors)
{
	return mors->hif.ops->init(mors);
}

static inline void mm81x_hif_flush_tx_data(struct mm81x *mors)
{
	mors->hif.ops->flush_tx_data(mors);
}

static inline void mm81x_hif_flush_cmds(struct mm81x *mors)
{
	mors->hif.ops->flush_cmds(mors);
}

static inline void mm81x_hif_finish(struct mm81x *mors)
{
	mors->hif.ops->finish(mors);
}

static inline void mm81x_hif_skbq_get_tx_qs(struct mm81x *mors,
					    struct mm81x_skbq **qs, int *num_qs)
{
	mors->hif.ops->skbq_get_tx_qs(mors, qs, num_qs);
}

static inline struct mm81x_skbq *mm81x_hif_get_tx_cmd_queue(struct mm81x *mors)
{
	return mors->hif.ops->get_tx_cmd_queue(mors);
}

static inline struct mm81x_skbq *
mm81x_hif_get_tx_beacon_queue(struct mm81x *mors)
{
	return mors->hif.ops->get_tx_beacon_queue(mors);
}

static inline struct mm81x_skbq *mm81x_hif_get_tx_mgmt_queue(struct mm81x *mors)
{
	return mors->hif.ops->get_tx_mgmt_queue(mors);
}

static inline struct mm81x_skbq *mm81x_hif_get_tx_data_queue(struct mm81x *mors,
							     int aci)
{
	return mors->hif.ops->get_tx_data_queue(mors, aci);
}

static inline int mm81x_hif_handle_irq(struct mm81x *mors, u32 status)
{
	return mors->hif.ops->handle_irq(mors, status);
}

static inline int mm81x_hif_get_tx_buffered_count(struct mm81x *mors)
{
	return mors->hif.ops->get_tx_buffered_count(mors);
}

static inline int mm81x_hif_get_tx_status_pending_count(struct mm81x *mors)
{
	return mors->hif.ops->get_tx_status_pending_count(mors);
}

#endif /* _MM81X_HIF_H_ */
