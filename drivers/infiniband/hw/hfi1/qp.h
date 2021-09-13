/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2015 - 2018 Intel Corporation.
 */

#ifndef _QP_H
#define _QP_H
#include <linux/hash.h>
#include <rdma/rdmavt_qp.h>
#include "verbs.h"
#include "sdma.h"
#include "verbs_txreq.h"

extern unsigned int hfi1_qp_table_size;

extern const struct rvt_operation_params hfi1_post_parms[];

/*
 * Driver specific s_flags starting at bit 31 down to HFI1_S_MIN_BIT_MASK
 *
 * HFI1_S_AHG_VALID - ahg header valid on chip
 * HFI1_S_AHG_CLEAR - have send engine clear ahg state
 * HFI1_S_WAIT_PIO_DRAIN - qp waiting for PIOs to drain
 * HFI1_S_WAIT_TID_SPACE - a QP is waiting for TID resource
 * HFI1_S_WAIT_TID_RESP - waiting for a TID RDMA WRITE response
 * HFI1_S_WAIT_HALT - halt the first leg send engine
 * HFI1_S_MIN_BIT_MASK - the lowest bit that can be used by hfi1
 */
#define HFI1_S_AHG_VALID         0x80000000
#define HFI1_S_AHG_CLEAR         0x40000000
#define HFI1_S_WAIT_PIO_DRAIN    0x20000000
#define HFI1_S_WAIT_TID_SPACE    0x10000000
#define HFI1_S_WAIT_TID_RESP     0x08000000
#define HFI1_S_WAIT_HALT         0x04000000
#define HFI1_S_MIN_BIT_MASK      0x01000000

/*
 * overload wait defines
 */

#define HFI1_S_ANY_WAIT_IO (RVT_S_ANY_WAIT_IO | HFI1_S_WAIT_PIO_DRAIN)
#define HFI1_S_ANY_WAIT (HFI1_S_ANY_WAIT_IO | RVT_S_ANY_WAIT_SEND)
#define HFI1_S_ANY_TID_WAIT_SEND (RVT_S_WAIT_SSN_CREDIT | RVT_S_WAIT_DMA)

/*
 * Send if not busy or waiting for I/O and either
 * a RC response is pending or we can process send work requests.
 */
static inline int hfi1_send_ok(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	return !(qp->s_flags & (RVT_S_BUSY | HFI1_S_ANY_WAIT_IO)) &&
		(verbs_txreq_queued(iowait_get_ib_work(&priv->s_iowait)) ||
		(qp->s_flags & RVT_S_RESP_PENDING) ||
		 !(qp->s_flags & RVT_S_ANY_WAIT_SEND));
}

/*
 * free_ahg - clear ahg from QP
 */
static inline void clear_ahg(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	priv->s_ahg->ahgcount = 0;
	qp->s_flags &= ~(HFI1_S_AHG_VALID | HFI1_S_AHG_CLEAR);
	if (priv->s_sde && qp->s_ahgidx >= 0)
		sdma_ahg_free(priv->s_sde, qp->s_ahgidx);
	qp->s_ahgidx = -1;
}

/**
 * hfi1_qp_wakeup - wake up on the indicated event
 * @qp: the QP
 * @flag: flag the qp on which the qp is stalled
 */
void hfi1_qp_wakeup(struct rvt_qp *qp, u32 flag);

struct sdma_engine *qp_to_sdma_engine(struct rvt_qp *qp, u8 sc5);
struct send_context *qp_to_send_context(struct rvt_qp *qp, u8 sc5);

void qp_iter_print(struct seq_file *s, struct rvt_qp_iter *iter);

bool _hfi1_schedule_send(struct rvt_qp *qp);
bool hfi1_schedule_send(struct rvt_qp *qp);

void hfi1_migrate_qp(struct rvt_qp *qp);

/*
 * Functions provided by hfi1 driver for rdmavt to use
 */
void *qp_priv_alloc(struct rvt_dev_info *rdi, struct rvt_qp *qp);
void qp_priv_free(struct rvt_dev_info *rdi, struct rvt_qp *qp);
unsigned free_all_qps(struct rvt_dev_info *rdi);
void notify_qp_reset(struct rvt_qp *qp);
int get_pmtu_from_attr(struct rvt_dev_info *rdi, struct rvt_qp *qp,
		       struct ib_qp_attr *attr);
void flush_qp_waiters(struct rvt_qp *qp);
void notify_error_qp(struct rvt_qp *qp);
void stop_send_queue(struct rvt_qp *qp);
void quiesce_qp(struct rvt_qp *qp);
u32 mtu_from_qp(struct rvt_dev_info *rdi, struct rvt_qp *qp, u32 pmtu);
int mtu_to_path_mtu(u32 mtu);
void hfi1_error_port_qps(struct hfi1_ibport *ibp, u8 sl);
void hfi1_qp_unbusy(struct rvt_qp *qp, struct iowait_work *wait);
#endif /* _QP_H */
