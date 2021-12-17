/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_ISCSI_H
#define _QED_ISCSI_H
#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/qed/tcp_common.h>
#include <linux/qed/qed_iscsi_if.h>
#include <linux/qed/qed_chain.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_mcp.h"
#include "qed_sp.h"

struct qed_iscsi_info {
	spinlock_t lock; /* Connection resources. */
	struct list_head free_list;
	u16 max_num_outstanding_tasks;
	void *event_context;
	iscsi_event_cb_t event_cb;
};

#if IS_ENABLED(CONFIG_QED_ISCSI)
int qed_iscsi_alloc(struct qed_hwfn *p_hwfn);

void qed_iscsi_setup(struct qed_hwfn *p_hwfn);

void qed_iscsi_free(struct qed_hwfn *p_hwfn);

/**
 * qed_get_protocol_stats_iscsi(): Fills provided statistics
 *                                 struct with statistics.
 *
 * @cdev: Qed dev pointer.
 * @stats: Points to struct that will be filled with statistics.
 *
 * Return: Void.
 */
void qed_get_protocol_stats_iscsi(struct qed_dev *cdev,
				  struct qed_mcp_iscsi_stats *stats);
#else /* IS_ENABLED(CONFIG_QED_ISCSI) */
static inline int qed_iscsi_alloc(struct qed_hwfn *p_hwfn)
{
	return -EINVAL;
}

static inline void qed_iscsi_setup(struct qed_hwfn *p_hwfn) {}

static inline void qed_iscsi_free(struct qed_hwfn *p_hwfn) {}

static inline void
qed_get_protocol_stats_iscsi(struct qed_dev *cdev,
			     struct qed_mcp_iscsi_stats *stats) {}
#endif /* IS_ENABLED(CONFIG_QED_ISCSI) */

#endif
