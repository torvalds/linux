/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
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

#ifdef CONFIG_QED_LL2
extern const struct qed_ll2_ops qed_ll2_ops_pass;
#endif

#if IS_ENABLED(CONFIG_QED_ISCSI)
struct qed_iscsi_info *qed_iscsi_alloc(struct qed_hwfn *p_hwfn);

void qed_iscsi_setup(struct qed_hwfn *p_hwfn,
		     struct qed_iscsi_info *p_iscsi_info);

void qed_iscsi_free(struct qed_hwfn *p_hwfn,
		    struct qed_iscsi_info *p_iscsi_info);
#else /* IS_ENABLED(CONFIG_QED_ISCSI) */
static inline struct qed_iscsi_info *qed_iscsi_alloc(
		struct qed_hwfn *p_hwfn) { return NULL; }
static inline void qed_iscsi_setup(struct qed_hwfn *p_hwfn,
				   struct qed_iscsi_info *p_iscsi_info) {}
static inline void qed_iscsi_free(struct qed_hwfn *p_hwfn,
				  struct qed_iscsi_info *p_iscsi_info) {}
#endif /* IS_ENABLED(CONFIG_QED_ISCSI) */

#endif
