/*
 * Copyright (c) 2007 Cisco Systems, Inc.  All rights reserved.
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

#include <config.h>

#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <alloca.h>

#include "ibverbs.h"

struct ibv_pd_1_0 {
	struct ibv_context_1_0 *context;
	uint32_t		handle;

	struct ibv_pd	       *real_pd;
};

struct ibv_mr_1_0 {
	struct ibv_context_1_0 *context;
	struct ibv_pd_1_0      *pd;
	uint32_t		handle;
	uint32_t		lkey;
	uint32_t		rkey;

	struct ibv_mr	       *real_mr;
};

struct ibv_srq_1_0 {
	struct ibv_context_1_0 *context;
	void		       *srq_context;
	struct ibv_pd_1_0      *pd;
	uint32_t		handle;

	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
	uint32_t		events_completed;

	struct ibv_srq	       *real_srq;
};

struct ibv_qp_init_attr_1_0 {
	void		       *qp_context;
	struct ibv_cq_1_0      *send_cq;
	struct ibv_cq_1_0      *recv_cq;
	struct ibv_srq_1_0     *srq;
	struct ibv_qp_cap	cap;
	enum ibv_qp_type	qp_type;
	int			sq_sig_all;
};

struct ibv_send_wr_1_0 {
	struct ibv_send_wr_1_0 *next;
	uint64_t		wr_id;
	struct ibv_sge	       *sg_list;
	int			num_sge;
	enum ibv_wr_opcode	opcode;
	int			send_flags;
	__be32			imm_data;
	union {
		struct {
			uint64_t	remote_addr;
			uint32_t	rkey;
		} rdma;
		struct {
			uint64_t	remote_addr;
			uint64_t	compare_add;
			uint64_t	swap;
			uint32_t	rkey;
		} atomic;
		struct {
			struct ibv_ah_1_0 *ah;
			uint32_t	remote_qpn;
			uint32_t	remote_qkey;
		} ud;
	} wr;
};

struct ibv_recv_wr_1_0 {
	struct ibv_recv_wr_1_0 *next;
	uint64_t		wr_id;
	struct ibv_sge	       *sg_list;
	int			num_sge;
};

struct ibv_qp_1_0 {
	struct ibv_context_1_0 *context;
	void		       *qp_context;
	struct ibv_pd_1_0      *pd;
	struct ibv_cq_1_0      *send_cq;
	struct ibv_cq_1_0      *recv_cq;
	struct ibv_srq_1_0     *srq;
	uint32_t		handle;
	uint32_t		qp_num;
	enum ibv_qp_state       state;
	enum ibv_qp_type	qp_type;

	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
	uint32_t		events_completed;

	struct ibv_qp	       *real_qp;
};

struct ibv_cq_1_0 {
	struct ibv_context_1_0 *context;
	void		       *cq_context;
	uint32_t		handle;
	int			cqe;

	pthread_mutex_t		mutex;
	pthread_cond_t		cond;
	uint32_t		comp_events_completed;
	uint32_t		async_events_completed;

	struct ibv_cq	       *real_cq;
};

struct ibv_ah_1_0 {
	struct ibv_context_1_0 *context;
	struct ibv_pd_1_0      *pd;
	uint32_t		handle;

	struct ibv_ah	       *real_ah;
};

struct ibv_device_1_0 {
	void		       *obsolete_sysfs_dev;
	void		       *obsolete_sysfs_ibdev;
	struct ibv_device      *real_device; /* was obsolete driver member */
	struct _ibv_device_ops	_ops;
};

struct ibv_context_ops_1_0 {
	int			(*query_device)(struct ibv_context *context,
					      struct ibv_device_attr *device_attr);
	int			(*query_port)(struct ibv_context *context, uint8_t port_num,
					      struct ibv_port_attr *port_attr);
	struct ibv_pd *		(*alloc_pd)(struct ibv_context *context);
	int			(*dealloc_pd)(struct ibv_pd *pd);
	struct ibv_mr *		(*reg_mr)(struct ibv_pd *pd, void *addr, size_t length,
					  int access);
	int			(*dereg_mr)(struct ibv_mr *mr);
	struct ibv_cq *		(*create_cq)(struct ibv_context *context, int cqe,
					     struct ibv_comp_channel *channel,
					     int comp_vector);
	int			(*poll_cq)(struct ibv_cq_1_0 *cq, int num_entries,
					   struct ibv_wc *wc);
	int			(*req_notify_cq)(struct ibv_cq_1_0 *cq,
						 int solicited_only);
	void			(*cq_event)(struct ibv_cq *cq);
	int			(*resize_cq)(struct ibv_cq *cq, int cqe);
	int			(*destroy_cq)(struct ibv_cq *cq);
	struct ibv_srq *	(*create_srq)(struct ibv_pd *pd,
					      struct ibv_srq_init_attr *srq_init_attr);
	int			(*modify_srq)(struct ibv_srq *srq,
					      struct ibv_srq_attr *srq_attr,
					      int srq_attr_mask);
	int			(*query_srq)(struct ibv_srq *srq,
					     struct ibv_srq_attr *srq_attr);
	int			(*destroy_srq)(struct ibv_srq *srq);
	int			(*post_srq_recv)(struct ibv_srq_1_0 *srq,
						 struct ibv_recv_wr_1_0 *recv_wr,
						 struct ibv_recv_wr_1_0 **bad_recv_wr);
	struct ibv_qp *		(*create_qp)(struct ibv_pd *pd, struct ibv_qp_init_attr *attr);
	int			(*query_qp)(struct ibv_qp *qp, struct ibv_qp_attr *attr,
					    int attr_mask,
					    struct ibv_qp_init_attr *init_attr);
	int			(*modify_qp)(struct ibv_qp *qp, struct ibv_qp_attr *attr,
					     int attr_mask);
	int			(*destroy_qp)(struct ibv_qp *qp);
	int			(*post_send)(struct ibv_qp_1_0 *qp,
					     struct ibv_send_wr_1_0 *wr,
					     struct ibv_send_wr_1_0 **bad_wr);
	int			(*post_recv)(struct ibv_qp_1_0 *qp,
					     struct ibv_recv_wr_1_0 *wr,
					     struct ibv_recv_wr_1_0 **bad_wr);
	struct ibv_ah *		(*create_ah)(struct ibv_pd *pd, struct ibv_ah_attr *attr);
	int			(*destroy_ah)(struct ibv_ah *ah);
	int			(*attach_mcast)(struct ibv_qp *qp, union ibv_gid *gid,
						uint16_t lid);
	int			(*detach_mcast)(struct ibv_qp *qp, union ibv_gid *gid,
						uint16_t lid);
};

struct ibv_context_1_0 {
	struct ibv_device_1_0	       *device;
	struct ibv_context_ops_1_0	ops;
	int				cmd_fd;
	int				async_fd;
	int				num_comp_vectors;

	struct ibv_context	       *real_context; /* was abi_compat member */
};

typedef struct ibv_device *(*ibv_driver_init_func_1_1)(const char *uverbs_sys_path,
						       int abi_version);

/* Hack to avoid GCC's -Wmissing-prototypes and the similar error from sparse
   with these prototypes. Symbol versionining requires the goofy names, the
   prototype must match the version in the historical 1.0 verbs.h.
 */
struct ibv_device_1_0 **__ibv_get_device_list_1_0(int *num);
void __ibv_free_device_list_1_0(struct ibv_device_1_0 **list);
const char *__ibv_get_device_name_1_0(struct ibv_device_1_0 *device);
__be64 __ibv_get_device_guid_1_0(struct ibv_device_1_0 *device);
struct ibv_context_1_0 *__ibv_open_device_1_0(struct ibv_device_1_0 *device);
int __ibv_close_device_1_0(struct ibv_context_1_0 *context);
int __ibv_get_async_event_1_0(struct ibv_context_1_0 *context,
			      struct ibv_async_event *event);
void __ibv_ack_async_event_1_0(struct ibv_async_event *event);
int __ibv_query_device_1_0(struct ibv_context_1_0 *context,
			   struct ibv_device_attr *device_attr);
int __ibv_query_port_1_0(struct ibv_context_1_0 *context, uint8_t port_num,
			 struct ibv_port_attr *port_attr);
int __ibv_query_gid_1_0(struct ibv_context_1_0 *context, uint8_t port_num,
			int index, union ibv_gid *gid);
int __ibv_query_pkey_1_0(struct ibv_context_1_0 *context, uint8_t port_num,
			 int index, __be16 *pkey);
struct ibv_pd_1_0 *__ibv_alloc_pd_1_0(struct ibv_context_1_0 *context);
int __ibv_dealloc_pd_1_0(struct ibv_pd_1_0 *pd);
struct ibv_mr_1_0 *__ibv_reg_mr_1_0(struct ibv_pd_1_0 *pd, void *addr,
				    size_t length, int access);
int __ibv_dereg_mr_1_0(struct ibv_mr_1_0 *mr);
struct ibv_cq_1_0 *__ibv_create_cq_1_0(struct ibv_context_1_0 *context, int cqe,
				       void *cq_context,
				       struct ibv_comp_channel *channel,
				       int comp_vector);
int __ibv_resize_cq_1_0(struct ibv_cq_1_0 *cq, int cqe);
int __ibv_destroy_cq_1_0(struct ibv_cq_1_0 *cq);
int __ibv_get_cq_event_1_0(struct ibv_comp_channel *channel,
			   struct ibv_cq_1_0 **cq, void **cq_context);
void __ibv_ack_cq_events_1_0(struct ibv_cq_1_0 *cq, unsigned int nevents);
struct ibv_srq_1_0 *
__ibv_create_srq_1_0(struct ibv_pd_1_0 *pd,
		     struct ibv_srq_init_attr *srq_init_attr);
int __ibv_modify_srq_1_0(struct ibv_srq_1_0 *srq, struct ibv_srq_attr *srq_attr,
			 int srq_attr_mask);
int __ibv_query_srq_1_0(struct ibv_srq_1_0 *srq, struct ibv_srq_attr *srq_attr);
int __ibv_destroy_srq_1_0(struct ibv_srq_1_0 *srq);
struct ibv_qp_1_0 *
__ibv_create_qp_1_0(struct ibv_pd_1_0 *pd,
		    struct ibv_qp_init_attr_1_0 *qp_init_attr);
int __ibv_query_qp_1_0(struct ibv_qp_1_0 *qp, struct ibv_qp_attr *attr,
		       int attr_mask, struct ibv_qp_init_attr_1_0 *init_attr);
int __ibv_modify_qp_1_0(struct ibv_qp_1_0 *qp, struct ibv_qp_attr *attr,
			int attr_mask);
int __ibv_destroy_qp_1_0(struct ibv_qp_1_0 *qp);
struct ibv_ah_1_0 *__ibv_create_ah_1_0(struct ibv_pd_1_0 *pd,
				       struct ibv_ah_attr *attr);
int __ibv_destroy_ah_1_0(struct ibv_ah_1_0 *ah);
int __ibv_attach_mcast_1_0(struct ibv_qp_1_0 *qp, union ibv_gid *gid,
			   uint16_t lid);
int __ibv_detach_mcast_1_0(struct ibv_qp_1_0 *qp, union ibv_gid *gid,
			   uint16_t lid);
void __ibv_register_driver_1_1(const char *name,
			       ibv_driver_init_func_1_1 init_func);

struct ibv_device_1_0 **__ibv_get_device_list_1_0(int *num)
{
	struct ibv_device **real_list;
	struct ibv_device_1_0 **l;
	int i, n;

	real_list = ibv_get_device_list(&n);
	if (!real_list)
		return NULL;

	l = calloc(n + 2, sizeof (struct ibv_device_1_0 *));
	if (!l)
		goto free_device_list;

	l[0] = (void *) real_list;

	for (i = 0; i < n; ++i) {
		l[i + 1] = calloc(1, sizeof (struct ibv_device_1_0));
		if (!l[i + 1])
			goto fail;
		l[i + 1]->real_device = real_list[i];
	}

	if (num)
		*num = n;

	return l + 1;

fail:
	for (i = 1; i <= n; ++i)
		if (l[i])
			free(l[i]);
	free(l);

free_device_list:
	ibv_free_device_list(real_list);
	return NULL;
}
symver(__ibv_get_device_list_1_0, ibv_get_device_list, IBVERBS_1.0);

void __ibv_free_device_list_1_0(struct ibv_device_1_0 **list)
{
	struct ibv_device_1_0 **l = list;

	while (*l) {
		free(*l);
		++l;
	}

	ibv_free_device_list((void *) list[-1]);
	free(list - 1);
}
symver(__ibv_free_device_list_1_0, ibv_free_device_list, IBVERBS_1.0);

const char *__ibv_get_device_name_1_0(struct ibv_device_1_0 *device)
{
	return ibv_get_device_name(device->real_device);
}
symver(__ibv_get_device_name_1_0, ibv_get_device_name, IBVERBS_1.0);

__be64 __ibv_get_device_guid_1_0(struct ibv_device_1_0 *device)
{
	return ibv_get_device_guid(device->real_device);
}
symver(__ibv_get_device_guid_1_0, ibv_get_device_guid, IBVERBS_1.0);

static int poll_cq_wrapper_1_0(struct ibv_cq_1_0 *cq, int num_entries,
			       struct ibv_wc *wc)
{
	return cq->context->real_context->ops.poll_cq(cq->real_cq, num_entries, wc);
}

static int req_notify_cq_wrapper_1_0(struct ibv_cq_1_0 *cq, int sol_only)
{
	return cq->context->real_context->ops.req_notify_cq(cq->real_cq, sol_only);
}

static int post_srq_recv_wrapper_1_0(struct ibv_srq_1_0 *srq, struct ibv_recv_wr_1_0 *wr,
				 struct ibv_recv_wr_1_0 **bad_wr)
{
	struct ibv_recv_wr_1_0 *w;
	struct ibv_recv_wr *real_wr, *head_wr = NULL, *tail_wr = NULL, *real_bad_wr;
	int ret;

	for (w = wr; w; w = w->next) {
		real_wr = alloca(sizeof *real_wr);
		real_wr->wr_id   = w->wr_id;
		real_wr->sg_list = w->sg_list;
		real_wr->num_sge = w->num_sge;
		real_wr->next    = NULL;
		if (tail_wr)
			tail_wr->next = real_wr;
		else
			head_wr = real_wr;

		tail_wr = real_wr;
	}

	ret = srq->context->real_context->ops.post_srq_recv(srq->real_srq, head_wr,
							    &real_bad_wr);

	if (ret) {
		for (real_wr = head_wr, w = wr;
		     real_wr;
		     real_wr = real_wr->next, w = w->next)
			if (real_wr == real_bad_wr) {
				*bad_wr = w;
				break;
			}
	}

	return ret;
}

static int post_send_wrapper_1_0(struct ibv_qp_1_0 *qp, struct ibv_send_wr_1_0 *wr,
				 struct ibv_send_wr_1_0 **bad_wr)
{
	struct ibv_send_wr_1_0 *w;
	struct ibv_send_wr *real_wr, *head_wr = NULL, *tail_wr = NULL, *real_bad_wr;
	int is_ud = qp->qp_type == IBV_QPT_UD;
	int ret;

	for (w = wr; w; w = w->next) {
		real_wr = alloca(sizeof *real_wr);
		real_wr->wr_id = w->wr_id;
		real_wr->next  = NULL;

#define TEST_SIZE_2_POINT(f1, f2)					\
		((offsetof(struct ibv_send_wr, f1) - offsetof(struct ibv_send_wr, f2)) \
		 == offsetof(struct ibv_send_wr_1_0, f1) - offsetof(struct ibv_send_wr_1_0, f2))
#define TEST_SIZE_TO_END(f1)					    \
		((sizeof(struct ibv_send_wr) - offsetof(struct ibv_send_wr, f1)) == \
		 (sizeof(struct ibv_send_wr_1_0) - offsetof(struct ibv_send_wr_1_0, f1)))

		if (TEST_SIZE_TO_END (sg_list))
			memcpy(&real_wr->sg_list, &w->sg_list, sizeof *real_wr
			       - offsetof(struct ibv_send_wr, sg_list));
		else if (TEST_SIZE_2_POINT (imm_data, sg_list) &&
			 TEST_SIZE_TO_END (wr)) {
			/* we have alignment up to wr, but padding between
			 * imm_data and wr, and we know wr itself is the
			 * same size */
			memcpy(&real_wr->sg_list, &w->sg_list,
			       offsetof(struct ibv_send_wr, imm_data) -
			       offsetof(struct ibv_send_wr, sg_list) +
			       sizeof real_wr->imm_data);
			memcpy(&real_wr->wr, &w->wr, sizeof real_wr->wr);
		} else {
			real_wr->sg_list = w->sg_list;
			real_wr->num_sge = w->num_sge;
			real_wr->opcode = w->opcode;
			real_wr->send_flags = w->send_flags;
			real_wr->imm_data = w->imm_data;
			if (TEST_SIZE_TO_END (wr))
				memcpy(&real_wr->wr, &w->wr,
				       sizeof real_wr->wr);
			else {
				real_wr->wr.atomic.remote_addr =
					w->wr.atomic.remote_addr;
				real_wr->wr.atomic.compare_add =
					w->wr.atomic.compare_add;
				real_wr->wr.atomic.swap =
					w->wr.atomic.swap;
				real_wr->wr.atomic.rkey =
					w->wr.atomic.rkey;
			}
		}

		if (is_ud)
			real_wr->wr.ud.ah = w->wr.ud.ah->real_ah;

		if (tail_wr)
			tail_wr->next = real_wr;
		else
			head_wr = real_wr;

		tail_wr = real_wr;
	}

	ret = qp->context->real_context->ops.post_send(qp->real_qp, head_wr,
						       &real_bad_wr);

	if (ret) {
		for (real_wr = head_wr, w = wr;
		     real_wr;
		     real_wr = real_wr->next, w = w->next)
			if (real_wr == real_bad_wr) {
				*bad_wr = w;
				break;
			}
	}

	return ret;
}

static int post_recv_wrapper_1_0(struct ibv_qp_1_0 *qp, struct ibv_recv_wr_1_0 *wr,
				 struct ibv_recv_wr_1_0 **bad_wr)
{
	struct ibv_recv_wr_1_0 *w;
	struct ibv_recv_wr *real_wr, *head_wr = NULL, *tail_wr = NULL, *real_bad_wr;
	int ret;

	for (w = wr; w; w = w->next) {
		real_wr = alloca(sizeof *real_wr);
		real_wr->wr_id   = w->wr_id;
		real_wr->sg_list = w->sg_list;
		real_wr->num_sge = w->num_sge;
		real_wr->next    = NULL;
		if (tail_wr)
			tail_wr->next = real_wr;
		else
			head_wr = real_wr;

		tail_wr = real_wr;
	}

	ret = qp->context->real_context->ops.post_recv(qp->real_qp, head_wr,
						       &real_bad_wr);

	if (ret) {
		for (real_wr = head_wr, w = wr;
		     real_wr;
		     real_wr = real_wr->next, w = w->next)
			if (real_wr == real_bad_wr) {
				*bad_wr = w;
				break;
			}
	}

	return ret;
}

struct ibv_context_1_0 *__ibv_open_device_1_0(struct ibv_device_1_0 *device)
{
	struct ibv_context     *real_ctx;
	struct ibv_context_1_0 *ctx;

	ctx = malloc(sizeof *ctx);
	if (!ctx)
		return NULL;

	real_ctx = ibv_open_device(device->real_device);
	if (!real_ctx) {
		free(ctx);
		return NULL;
	}

	ctx->device       = device;
	ctx->real_context = real_ctx;

	ctx->ops.poll_cq       = poll_cq_wrapper_1_0;
	ctx->ops.req_notify_cq = req_notify_cq_wrapper_1_0;
	ctx->ops.post_send     = post_send_wrapper_1_0;
	ctx->ops.post_recv     = post_recv_wrapper_1_0;
	ctx->ops.post_srq_recv = post_srq_recv_wrapper_1_0;

	return ctx;
}
symver(__ibv_open_device_1_0, ibv_open_device, IBVERBS_1.0);

int __ibv_close_device_1_0(struct ibv_context_1_0 *context)
{
	int ret;

	ret = ibv_close_device(context->real_context);
	if (ret)
		return ret;

	free(context);
	return 0;
}
symver(__ibv_close_device_1_0, ibv_close_device, IBVERBS_1.0);

int __ibv_get_async_event_1_0(struct ibv_context_1_0 *context,
			      struct ibv_async_event *event)
{
	int ret;

	ret = ibv_get_async_event(context->real_context, event);
	if (ret)
		return ret;

	switch (event->event_type) {
	case IBV_EVENT_CQ_ERR:
		event->element.cq = event->element.cq->cq_context;
		break;

	case IBV_EVENT_QP_FATAL:
	case IBV_EVENT_QP_REQ_ERR:
	case IBV_EVENT_QP_ACCESS_ERR:
	case IBV_EVENT_COMM_EST:
	case IBV_EVENT_SQ_DRAINED:
	case IBV_EVENT_PATH_MIG:
	case IBV_EVENT_PATH_MIG_ERR:
	case IBV_EVENT_QP_LAST_WQE_REACHED:
		event->element.qp = event->element.qp->qp_context;
		break;

	case IBV_EVENT_SRQ_ERR:
	case IBV_EVENT_SRQ_LIMIT_REACHED:
		event->element.srq = event->element.srq->srq_context;
		break;

	default:
		break;
	}

	return ret;
}
symver(__ibv_get_async_event_1_0, ibv_get_async_event, IBVERBS_1.0);

void __ibv_ack_async_event_1_0(struct ibv_async_event *event)
{
	struct ibv_async_event real_event = *event;

	switch (event->event_type) {
	case IBV_EVENT_CQ_ERR:
		real_event.element.cq =
			((struct ibv_cq_1_0 *) event->element.cq)->real_cq;
		break;

	case IBV_EVENT_QP_FATAL:
	case IBV_EVENT_QP_REQ_ERR:
	case IBV_EVENT_QP_ACCESS_ERR:
	case IBV_EVENT_COMM_EST:
	case IBV_EVENT_SQ_DRAINED:
	case IBV_EVENT_PATH_MIG:
	case IBV_EVENT_PATH_MIG_ERR:
	case IBV_EVENT_QP_LAST_WQE_REACHED:
		real_event.element.qp =
			((struct ibv_qp_1_0 *) event->element.qp)->real_qp;
		break;

	case IBV_EVENT_SRQ_ERR:
	case IBV_EVENT_SRQ_LIMIT_REACHED:
		real_event.element.srq =
			((struct ibv_srq_1_0 *) event->element.srq)->real_srq;
		break;

	default:
		break;
	}

	ibv_ack_async_event(&real_event);
}
symver(__ibv_ack_async_event_1_0, ibv_ack_async_event, IBVERBS_1.0);

int __ibv_query_device_1_0(struct ibv_context_1_0 *context,
			   struct ibv_device_attr *device_attr)
{
	return ibv_query_device(context->real_context, device_attr);
}
symver(__ibv_query_device_1_0, ibv_query_device, IBVERBS_1.0);

int __ibv_query_port_1_0(struct ibv_context_1_0 *context, uint8_t port_num,
			 struct ibv_port_attr *port_attr)
{
	return ibv_query_port(context->real_context, port_num, port_attr);
}
symver(__ibv_query_port_1_0, ibv_query_port, IBVERBS_1.0);

int __ibv_query_gid_1_0(struct ibv_context_1_0 *context, uint8_t port_num,
			int index, union ibv_gid *gid)
{
	return ibv_query_gid(context->real_context, port_num, index, gid);
}
symver(__ibv_query_gid_1_0, ibv_query_gid, IBVERBS_1.0);

int __ibv_query_pkey_1_0(struct ibv_context_1_0 *context, uint8_t port_num,
			 int index, __be16 *pkey)
{
	return ibv_query_pkey(context->real_context, port_num, index, pkey);
}
symver(__ibv_query_pkey_1_0, ibv_query_pkey, IBVERBS_1.0);

struct ibv_pd_1_0 *__ibv_alloc_pd_1_0(struct ibv_context_1_0 *context)
{
	struct ibv_pd *real_pd;
	struct ibv_pd_1_0 *pd;

	pd = malloc(sizeof *pd);
	if (!pd)
		return NULL;

	real_pd = ibv_alloc_pd(context->real_context);
	if (!real_pd) {
		free(pd);
		return NULL;
	}

	pd->context = context;
	pd->real_pd = real_pd;

	return pd;
}
symver(__ibv_alloc_pd_1_0, ibv_alloc_pd, IBVERBS_1.0);

int __ibv_dealloc_pd_1_0(struct ibv_pd_1_0 *pd)
{
	int ret;

	ret = ibv_dealloc_pd(pd->real_pd);
	if (ret)
		return ret;

	free(pd);
	return 0;
}
symver(__ibv_dealloc_pd_1_0, ibv_dealloc_pd, IBVERBS_1.0);

struct ibv_mr_1_0 *__ibv_reg_mr_1_0(struct ibv_pd_1_0 *pd, void *addr,
				    size_t length, int access)
{
	struct ibv_mr *real_mr;
	struct ibv_mr_1_0 *mr;

	mr = malloc(sizeof *mr);
	if (!mr)
		return NULL;

	real_mr = ibv_reg_mr(pd->real_pd, addr, length, access);
	if (!real_mr) {
		free(mr);
		return NULL;
	}

	mr->context = pd->context;
	mr->pd      = pd;
	mr->lkey    = real_mr->lkey;
	mr->rkey    = real_mr->rkey;
	mr->real_mr = real_mr;

	return mr;
}
symver(__ibv_reg_mr_1_0, ibv_reg_mr, IBVERBS_1.0);

int __ibv_dereg_mr_1_0(struct ibv_mr_1_0 *mr)
{
	int ret;

	ret = ibv_dereg_mr(mr->real_mr);
	if (ret)
		return ret;

	free(mr);
	return 0;
}
symver(__ibv_dereg_mr_1_0, ibv_dereg_mr, IBVERBS_1.0);

struct ibv_cq_1_0 *__ibv_create_cq_1_0(struct ibv_context_1_0 *context, int cqe,
				       void *cq_context,
				       struct ibv_comp_channel *channel,
				       int comp_vector)
{
	struct ibv_cq *real_cq;
	struct ibv_cq_1_0 *cq;

	cq = malloc(sizeof *cq);
	if (!cq)
		return NULL;

	real_cq = ibv_create_cq(context->real_context, cqe, cq_context,
				channel, comp_vector);
	if (!real_cq) {
		free(cq);
		return NULL;
	}

	cq->context    = context;
	cq->cq_context = cq_context;
	cq->cqe        = cqe;
	cq->real_cq    = real_cq;

	real_cq->cq_context = cq;

	return cq;
}
symver(__ibv_create_cq_1_0, ibv_create_cq, IBVERBS_1.0);

int __ibv_resize_cq_1_0(struct ibv_cq_1_0 *cq, int cqe)
{
	return ibv_resize_cq(cq->real_cq, cqe);
}
symver(__ibv_resize_cq_1_0, ibv_resize_cq, IBVERBS_1.0);

int __ibv_destroy_cq_1_0(struct ibv_cq_1_0 *cq)
{
	int ret;

	ret = ibv_destroy_cq(cq->real_cq);
	if (ret)
		return ret;

	free(cq);
	return 0;
}
symver(__ibv_destroy_cq_1_0, ibv_destroy_cq, IBVERBS_1.0);

int __ibv_get_cq_event_1_0(struct ibv_comp_channel *channel,
			   struct ibv_cq_1_0 **cq, void **cq_context)
{
	struct ibv_cq *real_cq;
	void *cq_ptr;
	int ret;

	ret = ibv_get_cq_event(channel, &real_cq, &cq_ptr);
	if (ret)
		return ret;

	*cq         = cq_ptr;
	*cq_context = (*cq)->cq_context;

	return 0;
}
symver(__ibv_get_cq_event_1_0, ibv_get_cq_event, IBVERBS_1.0);

void __ibv_ack_cq_events_1_0(struct ibv_cq_1_0 *cq, unsigned int nevents)
{
	ibv_ack_cq_events(cq->real_cq, nevents);
}
symver(__ibv_ack_cq_events_1_0, ibv_ack_cq_events, IBVERBS_1.0);

struct ibv_srq_1_0 *__ibv_create_srq_1_0(struct ibv_pd_1_0 *pd,
					 struct ibv_srq_init_attr *srq_init_attr)
{
	struct ibv_srq *real_srq;
	struct ibv_srq_1_0 *srq;

	srq = malloc(sizeof *srq);
	if (!srq)
		return NULL;

	real_srq = ibv_create_srq(pd->real_pd, srq_init_attr);
	if (!real_srq) {
		free(srq);
		return NULL;
	}

	srq->context     = pd->context;
	srq->srq_context = srq_init_attr->srq_context;
	srq->pd          = pd;
	srq->real_srq    = real_srq;

	real_srq->srq_context = srq;

	return srq;
}
symver(__ibv_create_srq_1_0, ibv_create_srq, IBVERBS_1.0);

int __ibv_modify_srq_1_0(struct ibv_srq_1_0 *srq,
			 struct ibv_srq_attr *srq_attr,
			 int srq_attr_mask)
{
	return ibv_modify_srq(srq->real_srq, srq_attr, srq_attr_mask);
}
symver(__ibv_modify_srq_1_0, ibv_modify_srq, IBVERBS_1.0);

int __ibv_query_srq_1_0(struct ibv_srq_1_0 *srq, struct ibv_srq_attr *srq_attr)
{
	return ibv_query_srq(srq->real_srq, srq_attr);
}
symver(__ibv_query_srq_1_0, ibv_query_srq, IBVERBS_1.0);

int __ibv_destroy_srq_1_0(struct ibv_srq_1_0 *srq)
{
	int ret;

	ret = ibv_destroy_srq(srq->real_srq);
	if (ret)
		return ret;

	free(srq);
	return 0;
}
symver(__ibv_destroy_srq_1_0, ibv_destroy_srq, IBVERBS_1.0);

struct ibv_qp_1_0 *__ibv_create_qp_1_0(struct ibv_pd_1_0 *pd,
				       struct ibv_qp_init_attr_1_0 *qp_init_attr)
{
	struct ibv_qp *real_qp;
	struct ibv_qp_1_0 *qp;
	struct ibv_qp_init_attr real_init_attr;

	qp = malloc(sizeof *qp);
	if (!qp)
		return NULL;

	real_init_attr.qp_context = qp_init_attr->qp_context;
	real_init_attr.send_cq    = qp_init_attr->send_cq->real_cq;
	real_init_attr.recv_cq    = qp_init_attr->recv_cq->real_cq;
	real_init_attr.srq        = qp_init_attr->srq ?
		qp_init_attr->srq->real_srq : NULL;
	real_init_attr.cap        = qp_init_attr->cap;
	real_init_attr.qp_type    = qp_init_attr->qp_type;
	real_init_attr.sq_sig_all = qp_init_attr->sq_sig_all;

	real_qp = ibv_create_qp(pd->real_pd, &real_init_attr);
	if (!real_qp) {
		free(qp);
		return NULL;
	}

	qp->context    = pd->context;
	qp->qp_context = qp_init_attr->qp_context;
	qp->pd         = pd;
	qp->send_cq    = qp_init_attr->send_cq;
	qp->recv_cq    = qp_init_attr->recv_cq;
	qp->srq        = qp_init_attr->srq;
	qp->qp_type    = qp_init_attr->qp_type;
	qp->qp_num     = real_qp->qp_num;
	qp->real_qp    = real_qp;

	qp_init_attr->cap = real_init_attr.cap;

	real_qp->qp_context = qp;

	return qp;
}
symver(__ibv_create_qp_1_0, ibv_create_qp, IBVERBS_1.0);

int __ibv_query_qp_1_0(struct ibv_qp_1_0 *qp, struct ibv_qp_attr *attr,
		       int attr_mask,
		       struct ibv_qp_init_attr_1_0 *init_attr)
{
	struct ibv_qp_init_attr real_init_attr;
	int ret;

	ret = ibv_query_qp(qp->real_qp, attr, attr_mask, &real_init_attr);
	if (ret)
		return ret;

	init_attr->qp_context = qp->qp_context;
	init_attr->send_cq    = real_init_attr.send_cq->cq_context;
	init_attr->recv_cq    = real_init_attr.recv_cq->cq_context;
	init_attr->srq        = real_init_attr.srq->srq_context;
	init_attr->qp_type    = real_init_attr.qp_type;
	init_attr->cap        = real_init_attr.cap;
	init_attr->sq_sig_all = real_init_attr.sq_sig_all;

	return 0;
}
symver(__ibv_query_qp_1_0, ibv_query_qp, IBVERBS_1.0);

int __ibv_modify_qp_1_0(struct ibv_qp_1_0 *qp, struct ibv_qp_attr *attr,
			int attr_mask)
{
	return ibv_modify_qp(qp->real_qp, attr, attr_mask);
}
symver(__ibv_modify_qp_1_0, ibv_modify_qp, IBVERBS_1.0);

int __ibv_destroy_qp_1_0(struct ibv_qp_1_0 *qp)
{
	int ret;

	ret = ibv_destroy_qp(qp->real_qp);
	if (ret)
		return ret;

	free(qp);
	return 0;
}
symver(__ibv_destroy_qp_1_0, ibv_destroy_qp, IBVERBS_1.0);

struct ibv_ah_1_0 *__ibv_create_ah_1_0(struct ibv_pd_1_0 *pd,
				       struct ibv_ah_attr *attr)
{
	struct ibv_ah *real_ah;
	struct ibv_ah_1_0 *ah;

	ah = malloc(sizeof *ah);
	if (!ah)
		return NULL;

	real_ah = ibv_create_ah(pd->real_pd, attr);
	if (!real_ah) {
		free(ah);
		return NULL;
	}

	ah->context = pd->context;
	ah->pd      = pd;
	ah->real_ah = real_ah;

	return ah;
}
symver(__ibv_create_ah_1_0, ibv_create_ah, IBVERBS_1.0);

int __ibv_destroy_ah_1_0(struct ibv_ah_1_0 *ah)
{
	int ret;

	ret = ibv_destroy_ah(ah->real_ah);
	if (ret)
		return ret;

	free(ah);
	return 0;
}
symver(__ibv_destroy_ah_1_0, ibv_destroy_ah, IBVERBS_1.0);

int __ibv_attach_mcast_1_0(struct ibv_qp_1_0 *qp, union ibv_gid *gid, uint16_t lid)
{
	return ibv_attach_mcast(qp->real_qp, gid, lid);
}
symver(__ibv_attach_mcast_1_0, ibv_attach_mcast, IBVERBS_1.0);

int __ibv_detach_mcast_1_0(struct ibv_qp_1_0 *qp, union ibv_gid *gid, uint16_t lid)
{
	return ibv_detach_mcast(qp->real_qp, gid, lid);
}
symver(__ibv_detach_mcast_1_0, ibv_detach_mcast, IBVERBS_1.0);

void __ibv_register_driver_1_1(const char *name, ibv_driver_init_func_1_1 init_func)
{
	/* The driver interface is private as of rdma-core 13. This stub is
	 * left to preserve dynamic-link compatibility with old libfabrics
	 * usnic providers which use this function only to suppress a fprintf
	 * in old versions of libibverbs. */
}
symver(__ibv_register_driver_1_1, ibv_register_driver, IBVERBS_1.1);
