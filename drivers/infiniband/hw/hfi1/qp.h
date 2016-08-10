#ifndef _QP_H
#define _QP_H
/*
 * Copyright(c) 2015, 2016 Intel Corporation.
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

extern unsigned int hfi1_qp_table_size;

extern const struct rvt_operation_params hfi1_post_parms[];

/*
 * free_ahg - clear ahg from QP
 */
static inline void clear_ahg(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;

	priv->s_ahg->ahgcount = 0;
	qp->s_flags &= ~(RVT_S_AHG_VALID | RVT_S_AHG_CLEAR);
	if (priv->s_sde && qp->s_ahgidx >= 0)
		sdma_ahg_free(priv->s_sde, qp->s_ahgidx);
	qp->s_ahgidx = -1;
}

/**
 * hfi1_compute_aeth - compute the AETH (syndrome + MSN)
 * @qp: the queue pair to compute the AETH for
 *
 * Returns the AETH.
 */
__be32 hfi1_compute_aeth(struct rvt_qp *qp);

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
 * hfi1_get_credit - flush the send work queue of a QP
 * @qp: the qp who's send work queue to flush
 * @aeth: the Acknowledge Extended Transport Header
 *
 * The QP s_lock should be held.
 */
void hfi1_get_credit(struct rvt_qp *qp, u32 aeth);

/**
 * hfi1_qp_wakeup - wake up on the indicated event
 * @qp: the QP
 * @flag: flag the qp on which the qp is stalled
 */
void hfi1_qp_wakeup(struct rvt_qp *qp, u32 flag);

struct sdma_engine *qp_to_sdma_engine(struct rvt_qp *qp, u8 sc5);
struct send_context *qp_to_send_context(struct rvt_qp *qp, u8 sc5);

struct qp_iter;

/**
 * qp_iter_init - initialize the iterator for the qp hash list
 * @dev: the hfi1_ibdev
 */
struct qp_iter *qp_iter_init(struct hfi1_ibdev *dev);

/**
 * qp_iter_next - Find the next qp in the hash list
 * @iter: the iterator for the qp hash list
 */
int qp_iter_next(struct qp_iter *iter);

/**
 * qp_iter_print - print the qp information to seq_file
 * @s: the seq_file to emit the qp information on
 * @iter: the iterator for the qp hash list
 */
void qp_iter_print(struct seq_file *s, struct qp_iter *iter);

/**
 * qp_comm_est - handle trap with QP established
 * @qp: the QP
 */
void qp_comm_est(struct rvt_qp *qp);

void _hfi1_schedule_send(struct rvt_qp *qp);
void hfi1_schedule_send(struct rvt_qp *qp);

void hfi1_migrate_qp(struct rvt_qp *qp);

/*
 * Functions provided by hfi1 driver for rdmavt to use
 */
void *qp_priv_alloc(struct rvt_dev_info *rdi, struct rvt_qp *qp,
		    gfp_t gfp);
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
#endif /* _QP_H */
