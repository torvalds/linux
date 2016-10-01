/* QLogic qed NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
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
#ifndef _QED_ROCE_H
#define _QED_ROCE_H
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_roce_if.h>
#include "qed.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"

#define QED_RDMA_MAX_FMR                    (RDMA_MAX_TIDS)
#define QED_RDMA_MAX_P_KEY                  (1)
#define QED_RDMA_MAX_WQE                    (0x7FFF)
#define QED_RDMA_MAX_SRQ_WQE_ELEM           (0x7FFF)
#define QED_RDMA_PAGE_SIZE_CAPS             (0xFFFFF000)
#define QED_RDMA_ACK_DELAY                  (15)
#define QED_RDMA_MAX_MR_SIZE                (0x10000000000ULL)
#define QED_RDMA_MAX_CQS                    (RDMA_MAX_CQS)
#define QED_RDMA_MAX_MRS                    (RDMA_MAX_TIDS)
/* Add 1 for header element */
#define QED_RDMA_MAX_SRQ_ELEM_PER_WQE	    (RDMA_MAX_SGE_PER_RQ_WQE + 1)
#define QED_RDMA_MAX_SGE_PER_SRQ_WQE        (RDMA_MAX_SGE_PER_RQ_WQE)
#define QED_RDMA_SRQ_WQE_ELEM_SIZE          (16)
#define QED_RDMA_MAX_SRQS                   (32 * 1024)

#define QED_RDMA_MAX_CQE_32_BIT             (0x7FFFFFFF - 1)
#define QED_RDMA_MAX_CQE_16_BIT             (0x7FFF - 1)

enum qed_rdma_toggle_bit {
	QED_RDMA_TOGGLE_BIT_CLEAR = 0,
	QED_RDMA_TOGGLE_BIT_SET = 1
};

struct qed_bmap {
	unsigned long *bitmap;
	u32 max_count;
};

struct qed_rdma_info {
	/* spin lock to protect bitmaps */
	spinlock_t lock;

	struct qed_bmap cq_map;
	struct qed_bmap pd_map;
	struct qed_bmap tid_map;
	struct qed_bmap qp_map;
	struct qed_bmap srq_map;
	struct qed_bmap cid_map;
	struct qed_bmap dpi_map;
	struct qed_bmap toggle_bits;
	struct qed_rdma_events events;
	struct qed_rdma_device *dev;
	struct qed_rdma_port *port;
	u32 last_tid;
	u8 num_cnqs;
	u32 num_qps;
	u32 num_mrs;
	u16 queue_zone_base;
	enum protocol_type proto;
};

int
qed_rdma_add_user(void *rdma_cxt,
		  struct qed_rdma_add_user_out_params *out_params);
int qed_rdma_alloc_pd(void *rdma_cxt, u16 *pd);
int qed_rdma_alloc_tid(void *rdma_cxt, u32 *tid);
int qed_rdma_deregister_tid(void *rdma_cxt, u32 tid);
void qed_rdma_free_tid(void *rdma_cxt, u32 tid);
struct qed_rdma_device *qed_rdma_query_device(void *rdma_cxt);
int
qed_rdma_register_tid(void *rdma_cxt,
		      struct qed_rdma_register_tid_in_params *params);
void qed_rdma_remove_user(void *rdma_cxt, u16 dpi);
int qed_rdma_start(void *p_hwfn, struct qed_rdma_start_in_params *params);
int qed_rdma_stop(void *rdma_cxt);
u32 qed_rdma_get_sb_id(void *p_hwfn, u32 rel_sb_id);
u32 qed_rdma_query_cau_timer_res(void *p_hwfn);
void qed_rdma_cnq_prod_update(void *rdma_cxt, u8 cnq_index, u16 prod);
void qed_rdma_resc_free(struct qed_hwfn *p_hwfn);
void qed_async_roce_event(struct qed_hwfn *p_hwfn,
			  struct event_ring_entry *p_eqe);

#if IS_ENABLED(CONFIG_INFINIBAND_QEDR)
void qed_rdma_dpm_bar(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);
#else
void qed_rdma_dpm_bar(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt) {}
#endif
#endif
