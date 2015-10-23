/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
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
 *
 */
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#include "usnic_log.h"
#include "usnic_vnic.h"
#include "usnic_fwd.h"
#include "usnic_uiom.h"
#include "usnic_debugfs.h"
#include "usnic_ib_qp_grp.h"
#include "usnic_ib_sysfs.h"
#include "usnic_transport.h"

#define DFLT_RQ_IDX	0

const char *usnic_ib_qp_grp_state_to_string(enum ib_qp_state state)
{
	switch (state) {
	case IB_QPS_RESET:
		return "Rst";
	case IB_QPS_INIT:
		return "Init";
	case IB_QPS_RTR:
		return "RTR";
	case IB_QPS_RTS:
		return "RTS";
	case IB_QPS_SQD:
		return "SQD";
	case IB_QPS_SQE:
		return "SQE";
	case IB_QPS_ERR:
		return "ERR";
	default:
		return "UNKOWN STATE";

	}
}

int usnic_ib_qp_grp_dump_hdr(char *buf, int buf_sz)
{
	return scnprintf(buf, buf_sz, "|QPN\t|State\t|PID\t|VF Idx\t|Fil ID");
}

int usnic_ib_qp_grp_dump_rows(void *obj, char *buf, int buf_sz)
{
	struct usnic_ib_qp_grp *qp_grp = obj;
	struct usnic_ib_qp_grp_flow *default_flow;
	if (obj) {
		default_flow = list_first_entry(&qp_grp->flows_lst,
					struct usnic_ib_qp_grp_flow, link);
		return scnprintf(buf, buf_sz, "|%d\t|%s\t|%d\t|%hu\t|%d",
					qp_grp->ibqp.qp_num,
					usnic_ib_qp_grp_state_to_string(
							qp_grp->state),
					qp_grp->owner_pid,
					usnic_vnic_get_index(qp_grp->vf->vnic),
					default_flow->flow->flow_id);
	} else {
		return scnprintf(buf, buf_sz, "|N/A\t|N/A\t|N/A\t|N/A\t|N/A");
	}
}

static struct usnic_vnic_res_chunk *
get_qp_res_chunk(struct usnic_ib_qp_grp *qp_grp)
{
	lockdep_assert_held(&qp_grp->lock);
	/*
	 * The QP res chunk, used to derive qp indices,
	 * are just indices of the RQs
	 */
	return usnic_ib_qp_grp_get_chunk(qp_grp, USNIC_VNIC_RES_TYPE_RQ);
}

static int enable_qp_grp(struct usnic_ib_qp_grp *qp_grp)
{

	int status;
	int i, vnic_idx;
	struct usnic_vnic_res_chunk *res_chunk;
	struct usnic_vnic_res *res;

	lockdep_assert_held(&qp_grp->lock);

	vnic_idx = usnic_vnic_get_index(qp_grp->vf->vnic);

	res_chunk = get_qp_res_chunk(qp_grp);
	if (IS_ERR_OR_NULL(res_chunk)) {
		usnic_err("Unable to get qp res with err %ld\n",
				PTR_ERR(res_chunk));
		return res_chunk ? PTR_ERR(res_chunk) : -ENOMEM;
	}

	for (i = 0; i < res_chunk->cnt; i++) {
		res = res_chunk->res[i];
		status = usnic_fwd_enable_qp(qp_grp->ufdev, vnic_idx,
						res->vnic_idx);
		if (status) {
			usnic_err("Failed to enable qp %d of %s:%d\n with err %d\n",
					res->vnic_idx, qp_grp->ufdev->name,
					vnic_idx, status);
			goto out_err;
		}
	}

	return 0;

out_err:
	for (i--; i >= 0; i--) {
		res = res_chunk->res[i];
		usnic_fwd_disable_qp(qp_grp->ufdev, vnic_idx,
					res->vnic_idx);
	}

	return status;
}

static int disable_qp_grp(struct usnic_ib_qp_grp *qp_grp)
{
	int i, vnic_idx;
	struct usnic_vnic_res_chunk *res_chunk;
	struct usnic_vnic_res *res;
	int status = 0;

	lockdep_assert_held(&qp_grp->lock);
	vnic_idx = usnic_vnic_get_index(qp_grp->vf->vnic);

	res_chunk = get_qp_res_chunk(qp_grp);
	if (IS_ERR_OR_NULL(res_chunk)) {
		usnic_err("Unable to get qp res with err %ld\n",
			PTR_ERR(res_chunk));
		return res_chunk ? PTR_ERR(res_chunk) : -ENOMEM;
	}

	for (i = 0; i < res_chunk->cnt; i++) {
		res = res_chunk->res[i];
		status = usnic_fwd_disable_qp(qp_grp->ufdev, vnic_idx,
						res->vnic_idx);
		if (status) {
			usnic_err("Failed to disable rq %d of %s:%d\n with err %d\n",
					res->vnic_idx,
					qp_grp->ufdev->name,
					vnic_idx, status);
		}
	}

	return status;

}

static int init_filter_action(struct usnic_ib_qp_grp *qp_grp,
				struct usnic_filter_action *uaction)
{
	struct usnic_vnic_res_chunk *res_chunk;

	res_chunk = usnic_ib_qp_grp_get_chunk(qp_grp, USNIC_VNIC_RES_TYPE_RQ);
	if (IS_ERR_OR_NULL(res_chunk)) {
		usnic_err("Unable to get %s with err %ld\n",
			usnic_vnic_res_type_to_str(USNIC_VNIC_RES_TYPE_RQ),
			PTR_ERR(res_chunk));
		return res_chunk ? PTR_ERR(res_chunk) : -ENOMEM;
	}

	uaction->vnic_idx = usnic_vnic_get_index(qp_grp->vf->vnic);
	uaction->action.type = FILTER_ACTION_RQ_STEERING;
	uaction->action.u.rq_idx = res_chunk->res[DFLT_RQ_IDX]->vnic_idx;

	return 0;
}

static struct usnic_ib_qp_grp_flow*
create_roce_custom_flow(struct usnic_ib_qp_grp *qp_grp,
			struct usnic_transport_spec *trans_spec)
{
	uint16_t port_num;
	int err;
	struct filter filter;
	struct usnic_filter_action uaction;
	struct usnic_ib_qp_grp_flow *qp_flow;
	struct usnic_fwd_flow *flow;
	enum usnic_transport_type trans_type;

	trans_type = trans_spec->trans_type;
	port_num = trans_spec->usnic_roce.port_num;

	/* Reserve Port */
	port_num = usnic_transport_rsrv_port(trans_type, port_num);
	if (port_num == 0)
		return ERR_PTR(-EINVAL);

	/* Create Flow */
	usnic_fwd_init_usnic_filter(&filter, port_num);
	err = init_filter_action(qp_grp, &uaction);
	if (err)
		goto out_unreserve_port;

	flow = usnic_fwd_alloc_flow(qp_grp->ufdev, &filter, &uaction);
	if (IS_ERR_OR_NULL(flow)) {
		usnic_err("Unable to alloc flow failed with err %ld\n",
				PTR_ERR(flow));
		err = flow ? PTR_ERR(flow) : -EFAULT;
		goto out_unreserve_port;
	}

	/* Create Flow Handle */
	qp_flow = kzalloc(sizeof(*qp_flow), GFP_ATOMIC);
	if (IS_ERR_OR_NULL(qp_flow)) {
		err = qp_flow ? PTR_ERR(qp_flow) : -ENOMEM;
		goto out_dealloc_flow;
	}
	qp_flow->flow = flow;
	qp_flow->trans_type = trans_type;
	qp_flow->usnic_roce.port_num = port_num;
	qp_flow->qp_grp = qp_grp;
	return qp_flow;

out_dealloc_flow:
	usnic_fwd_dealloc_flow(flow);
out_unreserve_port:
	usnic_transport_unrsrv_port(trans_type, port_num);
	return ERR_PTR(err);
}

static void release_roce_custom_flow(struct usnic_ib_qp_grp_flow *qp_flow)
{
	usnic_fwd_dealloc_flow(qp_flow->flow);
	usnic_transport_unrsrv_port(qp_flow->trans_type,
					qp_flow->usnic_roce.port_num);
	kfree(qp_flow);
}

static struct usnic_ib_qp_grp_flow*
create_udp_flow(struct usnic_ib_qp_grp *qp_grp,
		struct usnic_transport_spec *trans_spec)
{
	struct socket *sock;
	int sock_fd;
	int err;
	struct filter filter;
	struct usnic_filter_action uaction;
	struct usnic_ib_qp_grp_flow *qp_flow;
	struct usnic_fwd_flow *flow;
	enum usnic_transport_type trans_type;
	uint32_t addr;
	uint16_t port_num;
	int proto;

	trans_type = trans_spec->trans_type;
	sock_fd = trans_spec->udp.sock_fd;

	/* Get and check socket */
	sock = usnic_transport_get_socket(sock_fd);
	if (IS_ERR_OR_NULL(sock))
		return ERR_CAST(sock);

	err = usnic_transport_sock_get_addr(sock, &proto, &addr, &port_num);
	if (err)
		goto out_put_sock;

	if (proto != IPPROTO_UDP) {
		usnic_err("Protocol for fd %d is not UDP", sock_fd);
		err = -EPERM;
		goto out_put_sock;
	}

	/* Create flow */
	usnic_fwd_init_udp_filter(&filter, addr, port_num);
	err = init_filter_action(qp_grp, &uaction);
	if (err)
		goto out_put_sock;

	flow = usnic_fwd_alloc_flow(qp_grp->ufdev, &filter, &uaction);
	if (IS_ERR_OR_NULL(flow)) {
		usnic_err("Unable to alloc flow failed with err %ld\n",
				PTR_ERR(flow));
		err = flow ? PTR_ERR(flow) : -EFAULT;
		goto out_put_sock;
	}

	/* Create qp_flow */
	qp_flow = kzalloc(sizeof(*qp_flow), GFP_ATOMIC);
	if (IS_ERR_OR_NULL(qp_flow)) {
		err = qp_flow ? PTR_ERR(qp_flow) : -ENOMEM;
		goto out_dealloc_flow;
	}
	qp_flow->flow = flow;
	qp_flow->trans_type = trans_type;
	qp_flow->udp.sock = sock;
	qp_flow->qp_grp = qp_grp;
	return qp_flow;

out_dealloc_flow:
	usnic_fwd_dealloc_flow(flow);
out_put_sock:
	usnic_transport_put_socket(sock);
	return ERR_PTR(err);
}

static void release_udp_flow(struct usnic_ib_qp_grp_flow *qp_flow)
{
	usnic_fwd_dealloc_flow(qp_flow->flow);
	usnic_transport_put_socket(qp_flow->udp.sock);
	kfree(qp_flow);
}

static struct usnic_ib_qp_grp_flow*
create_and_add_flow(struct usnic_ib_qp_grp *qp_grp,
			struct usnic_transport_spec *trans_spec)
{
	struct usnic_ib_qp_grp_flow *qp_flow;
	enum usnic_transport_type trans_type;

	trans_type = trans_spec->trans_type;
	switch (trans_type) {
	case USNIC_TRANSPORT_ROCE_CUSTOM:
		qp_flow = create_roce_custom_flow(qp_grp, trans_spec);
		break;
	case USNIC_TRANSPORT_IPV4_UDP:
		qp_flow = create_udp_flow(qp_grp, trans_spec);
		break;
	default:
		usnic_err("Unsupported transport %u\n",
				trans_spec->trans_type);
		return ERR_PTR(-EINVAL);
	}

	if (!IS_ERR_OR_NULL(qp_flow)) {
		list_add_tail(&qp_flow->link, &qp_grp->flows_lst);
		usnic_debugfs_flow_add(qp_flow);
	}


	return qp_flow;
}

static void release_and_remove_flow(struct usnic_ib_qp_grp_flow *qp_flow)
{
	usnic_debugfs_flow_remove(qp_flow);
	list_del(&qp_flow->link);

	switch (qp_flow->trans_type) {
	case USNIC_TRANSPORT_ROCE_CUSTOM:
		release_roce_custom_flow(qp_flow);
		break;
	case USNIC_TRANSPORT_IPV4_UDP:
		release_udp_flow(qp_flow);
		break;
	default:
		WARN(1, "Unsupported transport %u\n",
				qp_flow->trans_type);
		break;
	}
}

static void release_and_remove_all_flows(struct usnic_ib_qp_grp *qp_grp)
{
	struct usnic_ib_qp_grp_flow *qp_flow, *tmp;
	list_for_each_entry_safe(qp_flow, tmp, &qp_grp->flows_lst, link)
		release_and_remove_flow(qp_flow);
}

int usnic_ib_qp_grp_modify(struct usnic_ib_qp_grp *qp_grp,
				enum ib_qp_state new_state,
				void *data)
{
	int status = 0;
	int vnic_idx;
	struct ib_event ib_event;
	enum ib_qp_state old_state;
	struct usnic_transport_spec *trans_spec;
	struct usnic_ib_qp_grp_flow *qp_flow;

	old_state = qp_grp->state;
	vnic_idx = usnic_vnic_get_index(qp_grp->vf->vnic);
	trans_spec = (struct usnic_transport_spec *) data;

	spin_lock(&qp_grp->lock);
	switch (new_state) {
	case IB_QPS_RESET:
		switch (old_state) {
		case IB_QPS_RESET:
			/* NO-OP */
			break;
		case IB_QPS_INIT:
			release_and_remove_all_flows(qp_grp);
			status = 0;
			break;
		case IB_QPS_RTR:
		case IB_QPS_RTS:
		case IB_QPS_ERR:
			status = disable_qp_grp(qp_grp);
			release_and_remove_all_flows(qp_grp);
			break;
		default:
			status = -EINVAL;
		}
		break;
	case IB_QPS_INIT:
		switch (old_state) {
		case IB_QPS_RESET:
			if (trans_spec) {
				qp_flow = create_and_add_flow(qp_grp,
								trans_spec);
				if (IS_ERR_OR_NULL(qp_flow)) {
					status = qp_flow ? PTR_ERR(qp_flow) : -EFAULT;
					break;
				}
			} else {
				/*
				 * Optional to specify filters.
				 */
				status = 0;
			}
			break;
		case IB_QPS_INIT:
			if (trans_spec) {
				qp_flow = create_and_add_flow(qp_grp,
								trans_spec);
				if (IS_ERR_OR_NULL(qp_flow)) {
					status = qp_flow ? PTR_ERR(qp_flow) : -EFAULT;
					break;
				}
			} else {
				/*
				 * Doesn't make sense to go into INIT state
				 * from INIT state w/o adding filters.
				 */
				status = -EINVAL;
			}
			break;
		case IB_QPS_RTR:
			status = disable_qp_grp(qp_grp);
			break;
		case IB_QPS_RTS:
			status = disable_qp_grp(qp_grp);
			break;
		default:
			status = -EINVAL;
		}
		break;
	case IB_QPS_RTR:
		switch (old_state) {
		case IB_QPS_INIT:
			status = enable_qp_grp(qp_grp);
			break;
		default:
			status = -EINVAL;
		}
		break;
	case IB_QPS_RTS:
		switch (old_state) {
		case IB_QPS_RTR:
			/* NO-OP FOR NOW */
			break;
		default:
			status = -EINVAL;
		}
		break;
	case IB_QPS_ERR:
		ib_event.device = &qp_grp->vf->pf->ib_dev;
		ib_event.element.qp = &qp_grp->ibqp;
		ib_event.event = IB_EVENT_QP_FATAL;

		switch (old_state) {
		case IB_QPS_RESET:
			qp_grp->ibqp.event_handler(&ib_event,
					qp_grp->ibqp.qp_context);
			break;
		case IB_QPS_INIT:
			release_and_remove_all_flows(qp_grp);
			qp_grp->ibqp.event_handler(&ib_event,
					qp_grp->ibqp.qp_context);
			break;
		case IB_QPS_RTR:
		case IB_QPS_RTS:
			status = disable_qp_grp(qp_grp);
			release_and_remove_all_flows(qp_grp);
			qp_grp->ibqp.event_handler(&ib_event,
					qp_grp->ibqp.qp_context);
			break;
		default:
			status = -EINVAL;
		}
		break;
	default:
		status = -EINVAL;
	}
	spin_unlock(&qp_grp->lock);

	if (!status) {
		qp_grp->state = new_state;
		usnic_info("Transistioned %u from %s to %s",
		qp_grp->grp_id,
		usnic_ib_qp_grp_state_to_string(old_state),
		usnic_ib_qp_grp_state_to_string(new_state));
	} else {
		usnic_err("Failed to transition %u from %s to %s",
		qp_grp->grp_id,
		usnic_ib_qp_grp_state_to_string(old_state),
		usnic_ib_qp_grp_state_to_string(new_state));
	}

	return status;
}

static struct usnic_vnic_res_chunk**
alloc_res_chunk_list(struct usnic_vnic *vnic,
			struct usnic_vnic_res_spec *res_spec, void *owner_obj)
{
	enum usnic_vnic_res_type res_type;
	struct usnic_vnic_res_chunk **res_chunk_list;
	int err, i, res_cnt, res_lst_sz;

	for (res_lst_sz = 0;
		res_spec->resources[res_lst_sz].type != USNIC_VNIC_RES_TYPE_EOL;
		res_lst_sz++) {
		/* Do Nothing */
	}

	res_chunk_list = kzalloc(sizeof(*res_chunk_list)*(res_lst_sz+1),
					GFP_ATOMIC);
	if (!res_chunk_list)
		return ERR_PTR(-ENOMEM);

	for (i = 0; res_spec->resources[i].type != USNIC_VNIC_RES_TYPE_EOL;
		i++) {
		res_type = res_spec->resources[i].type;
		res_cnt = res_spec->resources[i].cnt;

		res_chunk_list[i] = usnic_vnic_get_resources(vnic, res_type,
					res_cnt, owner_obj);
		if (IS_ERR_OR_NULL(res_chunk_list[i])) {
			err = res_chunk_list[i] ?
					PTR_ERR(res_chunk_list[i]) : -ENOMEM;
			usnic_err("Failed to get %s from %s with err %d\n",
				usnic_vnic_res_type_to_str(res_type),
				usnic_vnic_pci_name(vnic),
				err);
			goto out_free_res;
		}
	}

	return res_chunk_list;

out_free_res:
	for (i--; i > 0; i--)
		usnic_vnic_put_resources(res_chunk_list[i]);
	kfree(res_chunk_list);
	return ERR_PTR(err);
}

static void free_qp_grp_res(struct usnic_vnic_res_chunk **res_chunk_list)
{
	int i;
	for (i = 0; res_chunk_list[i]; i++)
		usnic_vnic_put_resources(res_chunk_list[i]);
	kfree(res_chunk_list);
}

static int qp_grp_and_vf_bind(struct usnic_ib_vf *vf,
				struct usnic_ib_pd *pd,
				struct usnic_ib_qp_grp *qp_grp)
{
	int err;
	struct pci_dev *pdev;

	lockdep_assert_held(&vf->lock);

	pdev = usnic_vnic_get_pdev(vf->vnic);
	if (vf->qp_grp_ref_cnt == 0) {
		err = usnic_uiom_attach_dev_to_pd(pd->umem_pd, &pdev->dev);
		if (err) {
			usnic_err("Failed to attach %s to domain\n",
					pci_name(pdev));
			return err;
		}
		vf->pd = pd;
	}
	vf->qp_grp_ref_cnt++;

	WARN_ON(vf->pd != pd);
	qp_grp->vf = vf;

	return 0;
}

static void qp_grp_and_vf_unbind(struct usnic_ib_qp_grp *qp_grp)
{
	struct pci_dev *pdev;
	struct usnic_ib_pd *pd;

	lockdep_assert_held(&qp_grp->vf->lock);

	pd = qp_grp->vf->pd;
	pdev = usnic_vnic_get_pdev(qp_grp->vf->vnic);
	if (--qp_grp->vf->qp_grp_ref_cnt == 0) {
		qp_grp->vf->pd = NULL;
		usnic_uiom_detach_dev_from_pd(pd->umem_pd, &pdev->dev);
	}
	qp_grp->vf = NULL;
}

static void log_spec(struct usnic_vnic_res_spec *res_spec)
{
	char buf[512];
	usnic_vnic_spec_dump(buf, sizeof(buf), res_spec);
	usnic_dbg("%s\n", buf);
}

static int qp_grp_id_from_flow(struct usnic_ib_qp_grp_flow *qp_flow,
				uint32_t *id)
{
	enum usnic_transport_type trans_type = qp_flow->trans_type;
	int err;
	uint16_t port_num = 0;

	switch (trans_type) {
	case USNIC_TRANSPORT_ROCE_CUSTOM:
		*id = qp_flow->usnic_roce.port_num;
		break;
	case USNIC_TRANSPORT_IPV4_UDP:
		err = usnic_transport_sock_get_addr(qp_flow->udp.sock,
							NULL, NULL,
							&port_num);
		if (err)
			return err;
		/*
		 * Copy port_num to stack first and then to *id,
		 * so that the short to int cast works for little
		 * and big endian systems.
		 */
		*id = port_num;
		break;
	default:
		usnic_err("Unsupported transport %u\n", trans_type);
		return -EINVAL;
	}

	return 0;
}

struct usnic_ib_qp_grp *
usnic_ib_qp_grp_create(struct usnic_fwd_dev *ufdev, struct usnic_ib_vf *vf,
			struct usnic_ib_pd *pd,
			struct usnic_vnic_res_spec *res_spec,
			struct usnic_transport_spec *transport_spec)
{
	struct usnic_ib_qp_grp *qp_grp;
	int err;
	enum usnic_transport_type transport = transport_spec->trans_type;
	struct usnic_ib_qp_grp_flow *qp_flow;

	lockdep_assert_held(&vf->lock);

	err = usnic_vnic_res_spec_satisfied(&min_transport_spec[transport],
						res_spec);
	if (err) {
		usnic_err("Spec does not meet miniumum req for transport %d\n",
				transport);
		log_spec(res_spec);
		return ERR_PTR(err);
	}

	qp_grp = kzalloc(sizeof(*qp_grp), GFP_ATOMIC);
	if (!qp_grp) {
		usnic_err("Unable to alloc qp_grp - Out of memory\n");
		return NULL;
	}

	qp_grp->res_chunk_list = alloc_res_chunk_list(vf->vnic, res_spec,
							qp_grp);
	if (IS_ERR_OR_NULL(qp_grp->res_chunk_list)) {
		err = qp_grp->res_chunk_list ?
				PTR_ERR(qp_grp->res_chunk_list) : -ENOMEM;
		usnic_err("Unable to alloc res for %d with err %d\n",
				qp_grp->grp_id, err);
		goto out_free_qp_grp;
	}

	err = qp_grp_and_vf_bind(vf, pd, qp_grp);
	if (err)
		goto out_free_res;

	INIT_LIST_HEAD(&qp_grp->flows_lst);
	spin_lock_init(&qp_grp->lock);
	qp_grp->ufdev = ufdev;
	qp_grp->state = IB_QPS_RESET;
	qp_grp->owner_pid = current->pid;

	qp_flow = create_and_add_flow(qp_grp, transport_spec);
	if (IS_ERR_OR_NULL(qp_flow)) {
		usnic_err("Unable to create and add flow with err %ld\n",
				PTR_ERR(qp_flow));
		err = qp_flow ? PTR_ERR(qp_flow) : -EFAULT;
		goto out_qp_grp_vf_unbind;
	}

	err = qp_grp_id_from_flow(qp_flow, &qp_grp->grp_id);
	if (err)
		goto out_release_flow;
	qp_grp->ibqp.qp_num = qp_grp->grp_id;

	usnic_ib_sysfs_qpn_add(qp_grp);

	return qp_grp;

out_release_flow:
	release_and_remove_flow(qp_flow);
out_qp_grp_vf_unbind:
	qp_grp_and_vf_unbind(qp_grp);
out_free_res:
	free_qp_grp_res(qp_grp->res_chunk_list);
out_free_qp_grp:
	kfree(qp_grp);

	return ERR_PTR(err);
}

void usnic_ib_qp_grp_destroy(struct usnic_ib_qp_grp *qp_grp)
{

	WARN_ON(qp_grp->state != IB_QPS_RESET);
	lockdep_assert_held(&qp_grp->vf->lock);

	release_and_remove_all_flows(qp_grp);
	usnic_ib_sysfs_qpn_remove(qp_grp);
	qp_grp_and_vf_unbind(qp_grp);
	free_qp_grp_res(qp_grp->res_chunk_list);
	kfree(qp_grp);
}

struct usnic_vnic_res_chunk*
usnic_ib_qp_grp_get_chunk(struct usnic_ib_qp_grp *qp_grp,
				enum usnic_vnic_res_type res_type)
{
	int i;

	for (i = 0; qp_grp->res_chunk_list[i]; i++) {
		if (qp_grp->res_chunk_list[i]->type == res_type)
			return qp_grp->res_chunk_list[i];
	}

	return ERR_PTR(-EINVAL);
}
