/*
 * Copyright (c) 2005 Ammasso, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
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
 *        disclaimer in the documentation and/or other materials
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
#ifndef _C2_VQ_H_
#define _C2_VQ_H_
#include <linux/sched.h>
#include "c2.h"
#include "c2_wr.h"
#include "c2_provider.h"

struct c2_vq_req {
	u64 reply_msg;		/* ptr to reply msg */
	wait_queue_head_t wait_object;	/* wait object for vq reqs */
	atomic_t reply_ready;	/* set when reply is ready */
	atomic_t refcnt;	/* used to cancel WRs... */
	int event;
	struct iw_cm_id *cm_id;
	struct c2_qp *qp;
};

extern int vq_init(struct c2_dev *c2dev);
extern void vq_term(struct c2_dev *c2dev);

extern struct c2_vq_req *vq_req_alloc(struct c2_dev *c2dev);
extern void vq_req_free(struct c2_dev *c2dev, struct c2_vq_req *req);
extern void vq_req_get(struct c2_dev *c2dev, struct c2_vq_req *req);
extern void vq_req_put(struct c2_dev *c2dev, struct c2_vq_req *req);
extern int vq_send_wr(struct c2_dev *c2dev, union c2wr * wr);

extern void *vq_repbuf_alloc(struct c2_dev *c2dev);
extern void vq_repbuf_free(struct c2_dev *c2dev, void *reply);

extern int vq_wait_for_reply(struct c2_dev *c2dev, struct c2_vq_req *req);
#endif				/* _C2_VQ_H_ */
