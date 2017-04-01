/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _QED_FCOE_H
#define _QED_FCOE_H
#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/qed/qed_fcoe_if.h>
#include <linux/qed/qed_chain.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_mcp.h"
#include "qed_sp.h"

struct qed_fcoe_info {
	spinlock_t lock; /* Connection resources. */
	struct list_head free_list;
};

#if IS_ENABLED(CONFIG_QED_FCOE)
struct qed_fcoe_info *qed_fcoe_alloc(struct qed_hwfn *p_hwfn);

void qed_fcoe_setup(struct qed_hwfn *p_hwfn, struct qed_fcoe_info *p_fcoe_info);

void qed_fcoe_free(struct qed_hwfn *p_hwfn, struct qed_fcoe_info *p_fcoe_info);
void qed_get_protocol_stats_fcoe(struct qed_dev *cdev,
				 struct qed_mcp_fcoe_stats *stats);
#else /* CONFIG_QED_FCOE */
static inline struct qed_fcoe_info *
qed_fcoe_alloc(struct qed_hwfn *p_hwfn)
{
	return NULL;
}

static inline void qed_fcoe_setup(struct qed_hwfn *p_hwfn,
				  struct qed_fcoe_info *p_fcoe_info)
{
}

static inline void qed_fcoe_free(struct qed_hwfn *p_hwfn,
				 struct qed_fcoe_info *p_fcoe_info)
{
}

static inline void qed_get_protocol_stats_fcoe(struct qed_dev *cdev,
					       struct qed_mcp_fcoe_stats *stats)
{
}
#endif /* CONFIG_QED_FCOE */

#ifdef CONFIG_QED_LL2
extern const struct qed_common_ops qed_common_ops_pass;
extern const struct qed_ll2_ops qed_ll2_ops_pass;
#endif

#endif /* _QED_FCOE_H */
