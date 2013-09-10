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
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#include "usnic_log.h"
#include "usnic_vnic.h"
#include "usnic_fwd.h"
#include "usnic_uiom.h"
#include "usnic_ib_qp_grp.h"
#include "usnic_ib_sysfs.h"
#include "usnic_transport.h"

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
	struct usnic_fwd_filter_hndl *default_filter_hndl;
	if (obj) {
		default_filter_hndl = list_first_entry(&qp_grp->filter_hndls,
					struct usnic_fwd_filter_hndl, link);
		return scnprintf(buf, buf_sz, "|%d\t|%s\t|%d\t|%hu\t|%d",
					qp_grp->ibqp.qp_num,
					usnic_ib_qp_grp_state_to_string(
							qp_grp->state),
					qp_grp->owner_pid,
					usnic_vnic_get_index(qp_grp->vf->vnic),
					default_filter_hndl->id);
	} else {
		return scnprintf(buf, buf_sz, "|N/A\t|N/A\t|N/A\t|N/A\t|N/A");
	}
}

static int add_fwd_filter(struct usnic_ib_qp_grp *qp_grp,
				struct usnic_fwd_filter *fwd_filter)
{
	struct usnic_fwd_filter_hndl *filter_hndl;
	int status;
	struct usnic_vnic_res_chunk *chunk;
	int rq_idx;

	WARN_ON(!spin_is_locked(&qp_grp->lock));

	chunk = usnic_ib_qp_grp_get_chunk(qp_grp, USNIC_VNIC_RES_TYPE_RQ);
	if (IS_ERR_OR_NULL(chunk) || chunk->cnt < 1) {
		usnic_err("Failed to get RQ info for qp_grp %u\n",
				qp_grp->grp_id);
		return -EFAULT;
	}

	rq_idx = chunk->res[0]->vnic_idx;

	switch (qp_grp->transport) {
	case USNIC_TRANSPORT_ROCE_CUSTOM:
		status = usnic_fwd_add_usnic_filter(qp_grp->ufdev,
					usnic_vnic_get_index(qp_grp->vf->vnic),
					rq_idx,
					fwd_filter,
					&filter_hndl);
		break;
	default:
		usnic_err("Unable to install filter for qp_grp %u for transport %d",
				qp_grp->grp_id, qp_grp->transport);
		status = -EINVAL;
	}

	if (status)
		return status;

	list_add_tail(&filter_hndl->link, &qp_grp->filter_hndls);
	return 0;
}

static int del_all_filters(struct usnic_ib_qp_grp *qp_grp)
{
	int err, status;
	struct usnic_fwd_filter_hndl *filter_hndl, *tmp;

	WARN_ON(!spin_is_locked(&qp_grp->lock));

	status = 0;

	list_for_each_entry_safe(filter_hndl, tmp,
					&qp_grp->filter_hndls, link) {
		list_del(&filter_hndl->link);
		err = usnic_fwd_del_filter(filter_hndl);
		if (err) {
			usnic_err("Failed to delete filter %u of qp_grp %d\n",
					filter_hndl->id, qp_grp->grp_id);
		}
		status |= err;
	}

	return status;
}

static int enable_qp_grp(struct usnic_ib_qp_grp *qp_grp)
{

	int status;
	int i, vnic_idx;
	struct usnic_vnic_res_chunk *res_chunk;
	struct usnic_vnic_res *res;

	WARN_ON(!spin_is_locked(&qp_grp->lock));

	vnic_idx = usnic_vnic_get_index(qp_grp->vf->vnic);

	res_chunk = usnic_ib_qp_grp_get_chunk(qp_grp, USNIC_VNIC_RES_TYPE_RQ);
	if (IS_ERR_OR_NULL(res_chunk)) {
		usnic_err("Unable to get %s with err %ld\n",
			usnic_vnic_res_type_to_str(USNIC_VNIC_RES_TYPE_RQ),
			PTR_ERR(res_chunk));
		return res_chunk ? PTR_ERR(res_chunk) : -ENOMEM;
	}

	for (i = 0; i < res_chunk->cnt; i++) {
		res = res_chunk->res[i];
		status = usnic_fwd_enable_rq(qp_grp->ufdev, vnic_idx,
						res->vnic_idx);
		if (status) {
			usnic_err("Failed to enable rq %d of %s:%d\n with err %d\n",
					res->vnic_idx,
					netdev_name(qp_grp->ufdev->netdev),
					vnic_idx, status);
			goto out_err;
		}
	}

	return 0;

out_err:
	for (i--; i >= 0; i--) {
		res = res_chunk->res[i];
		usnic_fwd_disable_rq(qp_grp->ufdev, vnic_idx,
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

	WARN_ON(!spin_is_locked(&qp_grp->lock));
	vnic_idx = usnic_vnic_get_index(qp_grp->vf->vnic);

	res_chunk = usnic_ib_qp_grp_get_chunk(qp_grp, USNIC_VNIC_RES_TYPE_RQ);
	if (IS_ERR_OR_NULL(res_chunk)) {
		usnic_err("Unable to get %s with err %ld\n",
			usnic_vnic_res_type_to_str(USNIC_VNIC_RES_TYPE_RQ),
			PTR_ERR(res_chunk));
		return res_chunk ? PTR_ERR(res_chunk) : -ENOMEM;
	}

	for (i = 0; i < res_chunk->cnt; i++) {
		res = res_chunk->res[i];
		status = usnic_fwd_disable_rq(qp_grp->ufdev, vnic_idx,
						res->vnic_idx);
		if (status) {
			usnic_err("Failed to disable rq %d of %s:%d\n with err %d\n",
					res->vnic_idx,
					netdev_name(qp_grp->ufdev->netdev),
					vnic_idx, status);
		}
	}

	return status;

}

int usnic_ib_qp_grp_modify(struct usnic_ib_qp_grp *qp_grp,
				enum ib_qp_state new_state,
				struct usnic_fwd_filter *fwd_filter)
{
	int status = 0;
	int vnic_idx;
	struct ib_event ib_event;
	enum ib_qp_state old_state;

	old_state = qp_grp->state;
	vnic_idx = usnic_vnic_get_index(qp_grp->vf->vnic);

	spin_lock(&qp_grp->lock);
	switch (new_state) {
	case IB_QPS_RESET:
		switch (old_state) {
		case IB_QPS_RESET:
			/* NO-OP */
			break;
		case IB_QPS_INIT:
			status = del_all_filters(qp_grp);
			break;
		case IB_QPS_RTR:
		case IB_QPS_RTS:
		case IB_QPS_ERR:
			status = disable_qp_grp(qp_grp);
			status &= del_all_filters(qp_grp);
			break;
		default:
			status = -EINVAL;
		}
		break;
	case IB_QPS_INIT:
		switch (old_state) {
		case IB_QPS_RESET:
			status = add_fwd_filter(qp_grp, fwd_filter);
			break;
		case IB_QPS_INIT:
			status = add_fwd_filter(qp_grp, fwd_filter);
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
			status = del_all_filters(qp_grp);
			qp_grp->ibqp.event_handler(&ib_event,
					qp_grp->ibqp.qp_context);
			break;
		case IB_QPS_RTR:
		case IB_QPS_RTS:
			status = disable_qp_grp(qp_grp);
			status &= del_all_filters(qp_grp);
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
		usnic_err("Failed to transistion %u from %s to %s",
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
			err = (res_chunk_list[i] ?
					PTR_ERR(res_chunk_list[i]) : -ENOMEM);
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

	WARN_ON(!spin_is_locked(&vf->lock));

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

	WARN_ON(!spin_is_locked(&qp_grp->vf->lock));

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

struct usnic_ib_qp_grp *
usnic_ib_qp_grp_create(struct usnic_fwd_dev *ufdev,
			struct usnic_ib_vf *vf,
			struct usnic_ib_pd *pd,
			struct usnic_vnic_res_spec *res_spec,
			enum usnic_transport_type transport)
{
	struct usnic_ib_qp_grp *qp_grp;
	u16 port_num;
	int err;

	WARN_ON(!spin_is_locked(&vf->lock));

	err = usnic_vnic_res_spec_satisfied(&min_transport_spec[transport],
						res_spec);
	if (err) {
		usnic_err("Spec does not meet miniumum req for transport %d\n",
				transport);
		log_spec(res_spec);
		return ERR_PTR(err);
	}

	port_num = usnic_transport_rsrv_port(transport, 0);
	if (!port_num) {
		usnic_err("Unable to allocate port for %s\n",
				netdev_name(ufdev->netdev));
		return ERR_PTR(-EINVAL);
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
		goto out_free_port;
	}

	INIT_LIST_HEAD(&qp_grp->filter_hndls);
	spin_lock_init(&qp_grp->lock);
	qp_grp->ufdev = ufdev;
	qp_grp->transport = transport;
	qp_grp->filters[DFLT_FILTER_IDX].transport = transport;
	qp_grp->filters[DFLT_FILTER_IDX].port_num = port_num;
	qp_grp->state = IB_QPS_RESET;
	qp_grp->owner_pid = current->pid;

	/* qp_num is same as default filter port_num */
	qp_grp->ibqp.qp_num = qp_grp->filters[DFLT_FILTER_IDX].port_num;
	qp_grp->grp_id = qp_grp->ibqp.qp_num;

	err = qp_grp_and_vf_bind(vf, pd, qp_grp);
	if (err)
		goto out_free_port;

	usnic_ib_sysfs_qpn_add(qp_grp);

	return qp_grp;

out_free_port:
	kfree(qp_grp);
	usnic_transport_unrsrv_port(transport, port_num);

	return ERR_PTR(err);
}

void usnic_ib_qp_grp_destroy(struct usnic_ib_qp_grp *qp_grp)
{
	u16 default_port_num;
	enum usnic_transport_type transport;

	WARN_ON(qp_grp->state != IB_QPS_RESET);
	WARN_ON(!spin_is_locked(&qp_grp->vf->lock));

	transport = qp_grp->filters[DFLT_FILTER_IDX].transport;
	default_port_num = qp_grp->filters[DFLT_FILTER_IDX].port_num;

	usnic_ib_sysfs_qpn_remove(qp_grp);
	qp_grp_and_vf_unbind(qp_grp);
	free_qp_grp_res(qp_grp->res_chunk_list);
	kfree(qp_grp);
	usnic_transport_unrsrv_port(transport, default_port_num);
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
