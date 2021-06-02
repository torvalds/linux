/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* Copyright 2021 Marvell. All rights reserved. */

#ifndef _QED_NVMETCP_H
#define _QED_NVMETCP_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/qed/tcp_common.h>
#include <linux/qed/qed_nvmetcp_if.h>
#include <linux/qed/qed_chain.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_mcp.h"
#include "qed_sp.h"

#define QED_NVMETCP_FW_CQ_SIZE (4 * 1024)

/* tcp parameters */
#define QED_TCP_TWO_MSL_TIMER 4000
#define QED_TCP_HALF_WAY_CLOSE_TIMEOUT 10
#define QED_TCP_MAX_FIN_RT 2
#define QED_TCP_SWS_TIMER 5000

struct qed_nvmetcp_info {
	spinlock_t lock; /* Connection resources. */
	struct list_head free_list;
	u16 max_num_outstanding_tasks;
	void *event_context;
	nvmetcp_event_cb_t event_cb;
};

#if IS_ENABLED(CONFIG_QED_NVMETCP)
int qed_nvmetcp_alloc(struct qed_hwfn *p_hwfn);
void qed_nvmetcp_setup(struct qed_hwfn *p_hwfn);
void qed_nvmetcp_free(struct qed_hwfn *p_hwfn);

#else /* IS_ENABLED(CONFIG_QED_NVMETCP) */
static inline int qed_nvmetcp_alloc(struct qed_hwfn *p_hwfn)
{
	return -EINVAL;
}

static inline void qed_nvmetcp_setup(struct qed_hwfn *p_hwfn) {}
static inline void qed_nvmetcp_free(struct qed_hwfn *p_hwfn) {}

#endif /* IS_ENABLED(CONFIG_QED_NVMETCP) */

#endif
