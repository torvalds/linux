/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef USNIC_IB_QP_GRP_H_
#define USNIC_IB_QP_GRP_H_

#include <rdma/ib_verbs.h>

#include "usnic_ib.h"
#include "usnic_abi.h"
#include "usnic_fwd.h"
#include "usnic_vnic.h"

#define MAX_QP_GRP_FILTERS	10
#define DFLT_FILTER_IDX		0

/*
 * The qp group struct represents all the hw resources needed to present a ib_qp
 */
struct usnic_ib_qp_grp {
	struct ib_qp				ibqp;
	enum ib_qp_state			state;
	int					grp_id;

	struct usnic_fwd_dev			*ufdev;
	short unsigned				filter_cnt;
	struct usnic_fwd_filter			filters[MAX_QP_GRP_FILTERS];
	struct list_head			filter_hndls;
	enum usnic_transport_type		transport;
	struct usnic_ib_ucontext		*ctx;

	struct usnic_vnic_res_chunk		**res_chunk_list;

	pid_t					owner_pid;
	struct usnic_ib_vf			*vf;
	struct list_head			link;

	spinlock_t				lock;

	struct kobject				kobj;
};

static const struct
usnic_vnic_res_spec min_transport_spec[USNIC_TRANSPORT_MAX] = {
	{ /*USNIC_TRANSPORT_UNKNOWN*/
		.resources = {
			{.type = USNIC_VNIC_RES_TYPE_EOL,	.cnt = 0,},
		},
	},
	{ /*USNIC_TRANSPORT_ROCE_CUSTOM*/
		.resources = {
			{.type = USNIC_VNIC_RES_TYPE_WQ,	.cnt = 1,},
			{.type = USNIC_VNIC_RES_TYPE_RQ,	.cnt = 1,},
			{.type = USNIC_VNIC_RES_TYPE_CQ,	.cnt = 1,},
			{.type = USNIC_VNIC_RES_TYPE_EOL,	.cnt = 0,},
		},
	},
};

const char *usnic_ib_qp_grp_state_to_string(enum ib_qp_state state);
int usnic_ib_qp_grp_dump_hdr(char *buf, int buf_sz);
int usnic_ib_qp_grp_dump_rows(void *obj, char *buf, int buf_sz);
struct usnic_ib_qp_grp *
usnic_ib_qp_grp_create(struct usnic_fwd_dev *ufdev, struct usnic_ib_vf *vf,
			struct usnic_ib_pd *pd,
			struct usnic_vnic_res_spec *res_spec,
			enum usnic_transport_type transport);
void usnic_ib_qp_grp_destroy(struct usnic_ib_qp_grp *qp_grp);
int usnic_ib_qp_grp_modify(struct usnic_ib_qp_grp *qp_grp,
				enum ib_qp_state new_state,
				struct usnic_fwd_filter *fwd_filter);
struct usnic_vnic_res_chunk
*usnic_ib_qp_grp_get_chunk(struct usnic_ib_qp_grp *qp_grp,
				enum usnic_vnic_res_type type);
static inline
struct usnic_ib_qp_grp *to_uqp_grp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct usnic_ib_qp_grp, ibqp);
}
#endif /* USNIC_IB_QP_GRP_H_ */
