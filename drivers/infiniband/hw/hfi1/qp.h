#ifndef _QP_H
#define _QP_H
/*
 * Copyright(c) 2015 - 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

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
 * HFI1_S_MIN_BIT_MASK - the lowest bit that can be used by hfi1
 */
#define HFI1_S_AHG_VALID         0x80000000
#define HFI1_S_AHG_CLEAR         0x40000000
#define HFI1_S_WAIT_PIO_DRAIN    0x20000000
#define HFI1_S_MIN_BIT_MASK      0x01000000

/*
 * overload wait defines
 */

#define HFI1_S_ANY_WAIT_IO (RVT_S_ANY_WAIT_IO | HFI1_S_WAIT_PIO_DRAIN)
#define HFI1_S_ANY_WAIT (HFI1_S_ANY_WAIT_IO | RVT_S_ANY_WAIT_SEND)

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
 * hfi1_create_qp - create a queue pair for a device
 * @ibpd: the protection domain who's device we create the queue pair for
 * @init_attr: the attributes of the queue pair
 * @udata: user data for libibverbs.so
 *
 * Returns the queue pair on success, otherwise returns an errno.
 *
 * Called by the ib_create_qp() core verbs function.
 */
struct ib_qp *hfi1_create_qp(struct ib_pd *ibpd,
			     struct ib_qp_init_attr *init_attr,
			     struct ib_udata *udata);

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
