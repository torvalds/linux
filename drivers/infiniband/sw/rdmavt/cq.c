/*
 * Copyright(c) 2015 Intel Corporation.
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

#include "cq.h"

/**
 * rvt_create_cq - create a completion queue
 * @ibdev: the device this completion queue is attached to
 * @attr: creation attributes
 * @context: unused by the QLogic_IB driver
 * @udata: user data for libibverbs.so
 *
 * Returns a pointer to the completion queue or negative errno values
 * for failure.
 *
 * Called by ib_create_cq() in the generic verbs code.
 */
struct ib_cq *rvt_create_cq(struct ib_device *ibdev,
			    const struct ib_cq_init_attr *attr,
			    struct ib_ucontext *context,
			    struct ib_udata *udata)
{
	return ERR_PTR(-EOPNOTSUPP);
}

/**
 * rvt_destroy_cq - destroy a completion queue
 * @ibcq: the completion queue to destroy.
 *
 * Returns 0 for success.
 *
 * Called by ib_destroy_cq() in the generic verbs code.
 */
int rvt_destroy_cq(struct ib_cq *ibcq)
{
	return -EOPNOTSUPP;
}

int rvt_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags notify_flags)
{
	return -EOPNOTSUPP;
}

/**
 * rvt_resize_cq - change the size of the CQ
 * @ibcq: the completion queue
 *
 * Returns 0 for success.
 */
int rvt_resize_cq(struct ib_cq *ibcq, int cqe, struct ib_udata *udata)
{
	return -EOPNOTSUPP;
}

/**
 * rvt_poll_cq - poll for work completion entries
 * @ibcq: the completion queue to poll
 * @num_entries: the maximum number of entries to return
 * @entry: pointer to array where work completions are placed
 *
 * Returns the number of completion entries polled.
 *
 * This may be called from interrupt context.  Also called by ib_poll_cq()
 * in the generic verbs code.
 */
int rvt_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *entry)
{
	return -EOPNOTSUPP;
}
