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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include <rdma/ib_user_verbs.h>
#include <rdma/ib_addr.h>
#include <rdma/uverbs_ioctl.h>

#include "usnic_abi.h"
#include "usnic_ib.h"
#include "usnic_common_util.h"
#include "usnic_ib_qp_grp.h"
#include "usnic_ib_verbs.h"
#include "usnic_fwd.h"
#include "usnic_log.h"
#include "usnic_uiom.h"
#include "usnic_transport.h"

#define USNIC_DEFAULT_TRANSPORT USNIC_TRANSPORT_ROCE_CUSTOM

const struct usnic_vnic_res_spec min_transport_spec[USNIC_TRANSPORT_MAX] = {
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
	{ /*USNIC_TRANSPORT_IPV4_UDP*/
		.resources = {
			{.type = USNIC_VNIC_RES_TYPE_WQ,	.cnt = 1,},
			{.type = USNIC_VNIC_RES_TYPE_RQ,	.cnt = 1,},
			{.type = USNIC_VNIC_RES_TYPE_CQ,	.cnt = 1,},
			{.type = USNIC_VNIC_RES_TYPE_EOL,	.cnt = 0,},
		},
	},
};

static void usnic_ib_fw_string_to_u64(char *fw_ver_str, u64 *fw_ver)
{
	*fw_ver = *((u64 *)fw_ver_str);
}

static int usnic_ib_fill_create_qp_resp(struct usnic_ib_qp_grp *qp_grp,
					struct ib_udata *udata)
{
	struct usnic_ib_dev *us_ibdev;
	struct usnic_ib_create_qp_resp resp;
	struct pci_dev *pdev;
	struct vnic_dev_bar *bar;
	struct usnic_vnic_res_chunk *chunk;
	struct usnic_ib_qp_grp_flow *default_flow;
	int i, err;

	memset(&resp, 0, sizeof(resp));

	us_ibdev = qp_grp->vf->pf;
	pdev = usnic_vnic_get_pdev(qp_grp->vf->vnic);
	if (!pdev) {
		usnic_err("Failed to get pdev of qp_grp %d\n",
				qp_grp->grp_id);
		return -EFAULT;
	}

	bar = usnic_vnic_get_bar(qp_grp->vf->vnic, 0);
	if (!bar) {
		usnic_err("Failed to get bar0 of qp_grp %d vf %s",
				qp_grp->grp_id, pci_name(pdev));
		return -EFAULT;
	}

	resp.vfid = usnic_vnic_get_index(qp_grp->vf->vnic);
	resp.bar_bus_addr = bar->bus_addr;
	resp.bar_len = bar->len;

	chunk = usnic_ib_qp_grp_get_chunk(qp_grp, USNIC_VNIC_RES_TYPE_RQ);
	if (IS_ERR(chunk)) {
		usnic_err("Failed to get chunk %s for qp_grp %d with err %ld\n",
			usnic_vnic_res_type_to_str(USNIC_VNIC_RES_TYPE_RQ),
			qp_grp->grp_id,
			PTR_ERR(chunk));
		return PTR_ERR(chunk);
	}

	WARN_ON(chunk->type != USNIC_VNIC_RES_TYPE_RQ);
	resp.rq_cnt = chunk->cnt;
	for (i = 0; i < chunk->cnt; i++)
		resp.rq_idx[i] = chunk->res[i]->vnic_idx;

	chunk = usnic_ib_qp_grp_get_chunk(qp_grp, USNIC_VNIC_RES_TYPE_WQ);
	if (IS_ERR(chunk)) {
		usnic_err("Failed to get chunk %s for qp_grp %d with err %ld\n",
			usnic_vnic_res_type_to_str(USNIC_VNIC_RES_TYPE_WQ),
			qp_grp->grp_id,
			PTR_ERR(chunk));
		return PTR_ERR(chunk);
	}

	WARN_ON(chunk->type != USNIC_VNIC_RES_TYPE_WQ);
	resp.wq_cnt = chunk->cnt;
	for (i = 0; i < chunk->cnt; i++)
		resp.wq_idx[i] = chunk->res[i]->vnic_idx;

	chunk = usnic_ib_qp_grp_get_chunk(qp_grp, USNIC_VNIC_RES_TYPE_CQ);
	if (IS_ERR(chunk)) {
		usnic_err("Failed to get chunk %s for qp_grp %d with err %ld\n",
			usnic_vnic_res_type_to_str(USNIC_VNIC_RES_TYPE_CQ),
			qp_grp->grp_id,
			PTR_ERR(chunk));
		return PTR_ERR(chunk);
	}

	WARN_ON(chunk->type != USNIC_VNIC_RES_TYPE_CQ);
	resp.cq_cnt = chunk->cnt;
	for (i = 0; i < chunk->cnt; i++)
		resp.cq_idx[i] = chunk->res[i]->vnic_idx;

	default_flow = list_first_entry(&qp_grp->flows_lst,
					struct usnic_ib_qp_grp_flow, link);
	resp.transport = default_flow->trans_type;

	err = ib_copy_to_udata(udata, &resp, sizeof(resp));
	if (err) {
		usnic_err("Failed to copy udata for %s",
			  dev_name(&us_ibdev->ib_dev.dev));
		return err;
	}

	return 0;
}

static int
find_free_vf_and_create_qp_grp(struct ib_qp *qp,
			       struct usnic_transport_spec *trans_spec,
			       struct usnic_vnic_res_spec *res_spec)
{
	struct usnic_ib_dev *us_ibdev = to_usdev(qp->device);
	struct usnic_ib_pd *pd = to_upd(qp->pd);
	struct usnic_ib_vf *vf;
	struct usnic_vnic *vnic;
	struct usnic_ib_qp_grp *qp_grp = to_uqp_grp(qp);
	struct device *dev, **dev_list;
	int i, ret;

	BUG_ON(!mutex_is_locked(&us_ibdev->usdev_lock));

	if (list_empty(&us_ibdev->vf_dev_list)) {
		usnic_info("No vfs to allocate\n");
		return -ENOMEM;
	}

	if (usnic_ib_share_vf) {
		/* Try to find resouces on a used vf which is in pd */
		dev_list = usnic_uiom_get_dev_list(pd->umem_pd);
		if (IS_ERR(dev_list))
			return PTR_ERR(dev_list);
		for (i = 0; dev_list[i]; i++) {
			dev = dev_list[i];
			vf = dev_get_drvdata(dev);
			mutex_lock(&vf->lock);
			vnic = vf->vnic;
			if (!usnic_vnic_check_room(vnic, res_spec)) {
				usnic_dbg("Found used vnic %s from %s\n",
						dev_name(&us_ibdev->ib_dev.dev),
						pci_name(usnic_vnic_get_pdev(
									vnic)));
				ret = usnic_ib_qp_grp_create(qp_grp,
							     us_ibdev->ufdev,
							     vf, pd, res_spec,
							     trans_spec);

				mutex_unlock(&vf->lock);
				goto qp_grp_check;
			}
			mutex_unlock(&vf->lock);

		}
		usnic_uiom_free_dev_list(dev_list);
		dev_list = NULL;
	}

	/* Try to find resources on an unused vf */
	list_for_each_entry(vf, &us_ibdev->vf_dev_list, link) {
		mutex_lock(&vf->lock);
		vnic = vf->vnic;
		if (vf->qp_grp_ref_cnt == 0 &&
		    usnic_vnic_check_room(vnic, res_spec) == 0) {
			ret = usnic_ib_qp_grp_create(qp_grp, us_ibdev->ufdev,
						     vf, pd, res_spec,
						     trans_spec);

			mutex_unlock(&vf->lock);
			goto qp_grp_check;
		}
		mutex_unlock(&vf->lock);
	}

	usnic_info("No free qp grp found on %s\n",
		   dev_name(&us_ibdev->ib_dev.dev));
	return -ENOMEM;

qp_grp_check:
	if (ret) {
		usnic_err("Failed to allocate qp_grp\n");
		if (usnic_ib_share_vf)
			usnic_uiom_free_dev_list(dev_list);
	}
	return ret;
}

static void qp_grp_destroy(struct usnic_ib_qp_grp *qp_grp)
{
	struct usnic_ib_vf *vf = qp_grp->vf;

	WARN_ON(qp_grp->state != IB_QPS_RESET);

	mutex_lock(&vf->lock);
	usnic_ib_qp_grp_destroy(qp_grp);
	mutex_unlock(&vf->lock);
}

static int create_qp_validate_user_data(struct usnic_ib_create_qp_cmd cmd)
{
	if (cmd.spec.trans_type <= USNIC_TRANSPORT_UNKNOWN ||
			cmd.spec.trans_type >= USNIC_TRANSPORT_MAX)
		return -EINVAL;

	return 0;
}

/* Start of ib callback functions */

enum rdma_link_layer usnic_ib_port_link_layer(struct ib_device *device,
					      u32 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

int usnic_ib_query_device(struct ib_device *ibdev,
			  struct ib_device_attr *props,
			  struct ib_udata *uhw)
{
	struct usnic_ib_dev *us_ibdev = to_usdev(ibdev);
	union ib_gid gid;
	struct ethtool_drvinfo info;
	int qp_per_vf;

	usnic_dbg("\n");
	if (uhw->inlen || uhw->outlen)
		return -EINVAL;

	mutex_lock(&us_ibdev->usdev_lock);
	us_ibdev->netdev->ethtool_ops->get_drvinfo(us_ibdev->netdev, &info);
	memset(props, 0, sizeof(*props));
	usnic_mac_ip_to_gid(us_ibdev->ufdev->mac, us_ibdev->ufdev->inaddr,
			&gid.raw[0]);
	memcpy(&props->sys_image_guid, &gid.global.interface_id,
		sizeof(gid.global.interface_id));
	usnic_ib_fw_string_to_u64(&info.fw_version[0], &props->fw_ver);
	props->max_mr_size = USNIC_UIOM_MAX_MR_SIZE;
	props->page_size_cap = USNIC_UIOM_PAGE_SIZE;
	props->vendor_id = PCI_VENDOR_ID_CISCO;
	props->vendor_part_id = PCI_DEVICE_ID_CISCO_VIC_USPACE_NIC;
	props->hw_ver = us_ibdev->pdev->subsystem_device;
	qp_per_vf = max(us_ibdev->vf_res_cnt[USNIC_VNIC_RES_TYPE_WQ],
			us_ibdev->vf_res_cnt[USNIC_VNIC_RES_TYPE_RQ]);
	props->max_qp = qp_per_vf *
		kref_read(&us_ibdev->vf_cnt);
	props->device_cap_flags = IB_DEVICE_PORT_ACTIVE_EVENT |
		IB_DEVICE_SYS_IMAGE_GUID | IB_DEVICE_BLOCK_MULTICAST_LOOPBACK;
	props->max_cq = us_ibdev->vf_res_cnt[USNIC_VNIC_RES_TYPE_CQ] *
		kref_read(&us_ibdev->vf_cnt);
	props->max_pd = USNIC_UIOM_MAX_PD_CNT;
	props->max_mr = USNIC_UIOM_MAX_MR_CNT;
	props->local_ca_ack_delay = 0;
	props->max_pkeys = 0;
	props->atomic_cap = IB_ATOMIC_NONE;
	props->masked_atomic_cap = props->atomic_cap;
	props->max_qp_rd_atom = 0;
	props->max_qp_init_rd_atom = 0;
	props->max_res_rd_atom = 0;
	props->max_srq = 0;
	props->max_srq_wr = 0;
	props->max_srq_sge = 0;
	props->max_fast_reg_page_list_len = 0;
	props->max_mcast_grp = 0;
	props->max_mcast_qp_attach = 0;
	props->max_total_mcast_qp_attach = 0;
	/* Owned by Userspace
	 * max_qp_wr, max_sge, max_sge_rd, max_cqe */
	mutex_unlock(&us_ibdev->usdev_lock);

	return 0;
}

int usnic_ib_query_port(struct ib_device *ibdev, u32 port,
				struct ib_port_attr *props)
{
	struct usnic_ib_dev *us_ibdev = to_usdev(ibdev);

	usnic_dbg("\n");

	if (ib_get_eth_speed(ibdev, port, &props->active_speed,
			     &props->active_width))
		return -EINVAL;

	/*
	 * usdev_lock is acquired after (and not before) ib_get_eth_speed call
	 * because acquiring rtnl_lock in ib_get_eth_speed, while holding
	 * usdev_lock could lead to a deadlock.
	 */
	mutex_lock(&us_ibdev->usdev_lock);
	/* props being zeroed by the caller, avoid zeroing it here */

	props->lid = 0;
	props->lmc = 1;
	props->sm_lid = 0;
	props->sm_sl = 0;

	if (!us_ibdev->ufdev->link_up) {
		props->state = IB_PORT_DOWN;
		props->phys_state = IB_PORT_PHYS_STATE_DISABLED;
	} else if (!us_ibdev->ufdev->inaddr) {
		props->state = IB_PORT_INIT;
		props->phys_state =
			IB_PORT_PHYS_STATE_PORT_CONFIGURATION_TRAINING;
	} else {
		props->state = IB_PORT_ACTIVE;
		props->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	}

	props->port_cap_flags = 0;
	props->gid_tbl_len = 1;
	props->bad_pkey_cntr = 0;
	props->qkey_viol_cntr = 0;
	props->max_mtu = IB_MTU_4096;
	props->active_mtu = iboe_get_mtu(us_ibdev->ufdev->mtu);
	/* Userspace will adjust for hdrs */
	props->max_msg_sz = us_ibdev->ufdev->mtu;
	props->max_vl_num = 1;
	mutex_unlock(&us_ibdev->usdev_lock);

	return 0;
}

int usnic_ib_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
				int qp_attr_mask,
				struct ib_qp_init_attr *qp_init_attr)
{
	struct usnic_ib_qp_grp *qp_grp;
	struct usnic_ib_vf *vf;
	int err;

	usnic_dbg("\n");

	memset(qp_attr, 0, sizeof(*qp_attr));
	memset(qp_init_attr, 0, sizeof(*qp_init_attr));

	qp_grp = to_uqp_grp(qp);
	vf = qp_grp->vf;
	mutex_lock(&vf->pf->usdev_lock);
	usnic_dbg("\n");
	qp_attr->qp_state = qp_grp->state;
	qp_attr->cur_qp_state = qp_grp->state;

	switch (qp_grp->ibqp.qp_type) {
	case IB_QPT_UD:
		qp_attr->qkey = 0;
		break;
	default:
		usnic_err("Unexpected qp_type %d\n", qp_grp->ibqp.qp_type);
		err = -EINVAL;
		goto err_out;
	}

	mutex_unlock(&vf->pf->usdev_lock);
	return 0;

err_out:
	mutex_unlock(&vf->pf->usdev_lock);
	return err;
}

int usnic_ib_query_gid(struct ib_device *ibdev, u32 port, int index,
				union ib_gid *gid)
{

	struct usnic_ib_dev *us_ibdev = to_usdev(ibdev);
	usnic_dbg("\n");

	if (index > 1)
		return -EINVAL;

	mutex_lock(&us_ibdev->usdev_lock);
	memset(&(gid->raw[0]), 0, sizeof(gid->raw));
	usnic_mac_ip_to_gid(us_ibdev->ufdev->mac, us_ibdev->ufdev->inaddr,
			&gid->raw[0]);
	mutex_unlock(&us_ibdev->usdev_lock);

	return 0;
}

int usnic_ib_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct usnic_ib_pd *pd = to_upd(ibpd);

	pd->umem_pd = usnic_uiom_alloc_pd();
	if (IS_ERR(pd->umem_pd))
		return PTR_ERR(pd->umem_pd);

	return 0;
}

int usnic_ib_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
	usnic_uiom_dealloc_pd((to_upd(pd))->umem_pd);
	return 0;
}

int usnic_ib_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *init_attr,
		       struct ib_udata *udata)
{
	int err;
	struct usnic_ib_dev *us_ibdev;
	struct usnic_ib_qp_grp *qp_grp = to_uqp_grp(ibqp);
	struct usnic_ib_ucontext *ucontext = rdma_udata_to_drv_context(
		udata, struct usnic_ib_ucontext, ibucontext);
	int cq_cnt;
	struct usnic_vnic_res_spec res_spec;
	struct usnic_ib_create_qp_cmd cmd;
	struct usnic_transport_spec trans_spec;

	usnic_dbg("\n");

	us_ibdev = to_usdev(ibqp->device);

	if (init_attr->create_flags)
		return -EOPNOTSUPP;

	err = ib_copy_from_udata(&cmd, udata, sizeof(cmd));
	if (err) {
		usnic_err("%s: cannot copy udata for create_qp\n",
			  dev_name(&us_ibdev->ib_dev.dev));
		return -EINVAL;
	}

	err = create_qp_validate_user_data(cmd);
	if (err) {
		usnic_err("%s: Failed to validate user data\n",
			  dev_name(&us_ibdev->ib_dev.dev));
		return -EINVAL;
	}

	if (init_attr->qp_type != IB_QPT_UD) {
		usnic_err("%s asked to make a non-UD QP: %d\n",
			  dev_name(&us_ibdev->ib_dev.dev), init_attr->qp_type);
		return -EOPNOTSUPP;
	}

	trans_spec = cmd.spec;
	mutex_lock(&us_ibdev->usdev_lock);
	cq_cnt = (init_attr->send_cq == init_attr->recv_cq) ? 1 : 2;
	res_spec = min_transport_spec[trans_spec.trans_type];
	usnic_vnic_res_spec_update(&res_spec, USNIC_VNIC_RES_TYPE_CQ, cq_cnt);
	err = find_free_vf_and_create_qp_grp(ibqp, &trans_spec, &res_spec);
	if (err)
		goto out_release_mutex;

	err = usnic_ib_fill_create_qp_resp(qp_grp, udata);
	if (err) {
		err = -EBUSY;
		goto out_release_qp_grp;
	}

	qp_grp->ctx = ucontext;
	list_add_tail(&qp_grp->link, &ucontext->qp_grp_list);
	usnic_ib_log_vf(qp_grp->vf);
	mutex_unlock(&us_ibdev->usdev_lock);
	return 0;

out_release_qp_grp:
	qp_grp_destroy(qp_grp);
out_release_mutex:
	mutex_unlock(&us_ibdev->usdev_lock);
	return err;
}

int usnic_ib_destroy_qp(struct ib_qp *qp, struct ib_udata *udata)
{
	struct usnic_ib_qp_grp *qp_grp;
	struct usnic_ib_vf *vf;

	usnic_dbg("\n");

	qp_grp = to_uqp_grp(qp);
	vf = qp_grp->vf;
	mutex_lock(&vf->pf->usdev_lock);
	if (usnic_ib_qp_grp_modify(qp_grp, IB_QPS_RESET, NULL)) {
		usnic_err("Failed to move qp grp %u to reset\n",
				qp_grp->grp_id);
	}

	list_del(&qp_grp->link);
	qp_grp_destroy(qp_grp);
	mutex_unlock(&vf->pf->usdev_lock);

	return 0;
}

int usnic_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
				int attr_mask, struct ib_udata *udata)
{
	struct usnic_ib_qp_grp *qp_grp;
	int status;
	usnic_dbg("\n");

	if (attr_mask & ~IB_QP_ATTR_STANDARD_BITS)
		return -EOPNOTSUPP;

	qp_grp = to_uqp_grp(ibqp);

	mutex_lock(&qp_grp->vf->pf->usdev_lock);
	if ((attr_mask & IB_QP_PORT) && attr->port_num != 1) {
		/* usnic devices only have one port */
		status = -EINVAL;
		goto out_unlock;
	}
	if (attr_mask & IB_QP_STATE) {
		status = usnic_ib_qp_grp_modify(qp_grp, attr->qp_state, NULL);
	} else {
		usnic_err("Unhandled request, attr_mask=0x%x\n", attr_mask);
		status = -EINVAL;
	}

out_unlock:
	mutex_unlock(&qp_grp->vf->pf->usdev_lock);
	return status;
}

int usnic_ib_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		       struct ib_udata *udata)
{
	if (attr->flags)
		return -EOPNOTSUPP;

	return 0;
}

int usnic_ib_destroy_cq(struct ib_cq *cq, struct ib_udata *udata)
{
	return 0;
}

struct ib_mr *usnic_ib_reg_mr(struct ib_pd *pd, u64 start, u64 length,
					u64 virt_addr, int access_flags,
					struct ib_udata *udata)
{
	struct usnic_ib_mr *mr;
	int err;

	usnic_dbg("start 0x%llx va 0x%llx length 0x%llx\n", start,
			virt_addr, length);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->umem = usnic_uiom_reg_get(to_upd(pd)->umem_pd, start, length,
					access_flags, 0);
	if (IS_ERR_OR_NULL(mr->umem)) {
		err = mr->umem ? PTR_ERR(mr->umem) : -EFAULT;
		goto err_free;
	}

	mr->ibmr.lkey = mr->ibmr.rkey = 0;
	return &mr->ibmr;

err_free:
	kfree(mr);
	return ERR_PTR(err);
}

int usnic_ib_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct usnic_ib_mr *mr = to_umr(ibmr);

	usnic_dbg("va 0x%lx length 0x%zx\n", mr->umem->va, mr->umem->length);

	usnic_uiom_reg_release(mr->umem);
	kfree(mr);
	return 0;
}

int usnic_ib_alloc_ucontext(struct ib_ucontext *uctx, struct ib_udata *udata)
{
	struct ib_device *ibdev = uctx->device;
	struct usnic_ib_ucontext *context = to_ucontext(uctx);
	struct usnic_ib_dev *us_ibdev = to_usdev(ibdev);
	usnic_dbg("\n");

	INIT_LIST_HEAD(&context->qp_grp_list);
	mutex_lock(&us_ibdev->usdev_lock);
	list_add_tail(&context->link, &us_ibdev->ctx_list);
	mutex_unlock(&us_ibdev->usdev_lock);

	return 0;
}

void usnic_ib_dealloc_ucontext(struct ib_ucontext *ibcontext)
{
	struct usnic_ib_ucontext *context = to_uucontext(ibcontext);
	struct usnic_ib_dev *us_ibdev = to_usdev(ibcontext->device);
	usnic_dbg("\n");

	mutex_lock(&us_ibdev->usdev_lock);
	WARN_ON_ONCE(!list_empty(&context->qp_grp_list));
	list_del(&context->link);
	mutex_unlock(&us_ibdev->usdev_lock);
}

int usnic_ib_mmap(struct ib_ucontext *context,
				struct vm_area_struct *vma)
{
	struct usnic_ib_ucontext *uctx = to_ucontext(context);
	struct usnic_ib_dev *us_ibdev;
	struct usnic_ib_qp_grp *qp_grp;
	struct usnic_ib_vf *vf;
	struct vnic_dev_bar *bar;
	dma_addr_t bus_addr;
	unsigned int len;
	unsigned int vfid;

	usnic_dbg("\n");

	us_ibdev = to_usdev(context->device);
	vma->vm_flags |= VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vfid = vma->vm_pgoff;
	usnic_dbg("Page Offset %lu PAGE_SHIFT %u VFID %u\n",
			vma->vm_pgoff, PAGE_SHIFT, vfid);

	mutex_lock(&us_ibdev->usdev_lock);
	list_for_each_entry(qp_grp, &uctx->qp_grp_list, link) {
		vf = qp_grp->vf;
		if (usnic_vnic_get_index(vf->vnic) == vfid) {
			bar = usnic_vnic_get_bar(vf->vnic, 0);
			if ((vma->vm_end - vma->vm_start) != bar->len) {
				usnic_err("Bar0 Len %lu - Request map %lu\n",
						bar->len,
						vma->vm_end - vma->vm_start);
				mutex_unlock(&us_ibdev->usdev_lock);
				return -EINVAL;
			}
			bus_addr = bar->bus_addr;
			len = bar->len;
			usnic_dbg("bus: %pa vaddr: %p size: %ld\n",
					&bus_addr, bar->vaddr, bar->len);
			mutex_unlock(&us_ibdev->usdev_lock);

			return remap_pfn_range(vma,
						vma->vm_start,
						bus_addr >> PAGE_SHIFT,
						len, vma->vm_page_prot);
		}
	}

	mutex_unlock(&us_ibdev->usdev_lock);
	usnic_err("No VF %u found\n", vfid);
	return -EINVAL;
}

