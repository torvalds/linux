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
#include <linux/netdevice.h>
#include <linux/pci.h>

#include "enic_api.h"
#include "usnic_common_pkt_hdr.h"
#include "usnic_fwd.h"
#include "usnic_log.h"

static int usnic_fwd_devcmd_locked(struct usnic_fwd_dev *ufdev, int vnic_idx,
					enum vnic_devcmd_cmd cmd, u64 *a0,
					u64 *a1)
{
	int status;
	struct net_device *netdev = ufdev->netdev;

	lockdep_assert_held(&ufdev->lock);

	status = enic_api_devcmd_proxy_by_index(netdev,
			vnic_idx,
			cmd,
			a0, a1,
			1000);
	if (status) {
		if (status == ERR_EINVAL && cmd == CMD_DEL_FILTER) {
			usnic_dbg("Dev %s vnic idx %u cmd %u already deleted",
					ufdev->name, vnic_idx, cmd);
		} else {
			usnic_err("Dev %s vnic idx %u cmd %u failed with status %d\n",
					ufdev->name, vnic_idx, cmd,
					status);
		}
	} else {
		usnic_dbg("Dev %s vnic idx %u cmd %u success",
				ufdev->name, vnic_idx, cmd);
	}

	return status;
}

static int usnic_fwd_devcmd(struct usnic_fwd_dev *ufdev, int vnic_idx,
				enum vnic_devcmd_cmd cmd, u64 *a0, u64 *a1)
{
	int status;

	spin_lock(&ufdev->lock);
	status = usnic_fwd_devcmd_locked(ufdev, vnic_idx, cmd, a0, a1);
	spin_unlock(&ufdev->lock);

	return status;
}

struct usnic_fwd_dev *usnic_fwd_dev_alloc(struct pci_dev *pdev)
{
	struct usnic_fwd_dev *ufdev;

	ufdev = kzalloc(sizeof(*ufdev), GFP_KERNEL);
	if (!ufdev)
		return NULL;

	ufdev->pdev = pdev;
	ufdev->netdev = pci_get_drvdata(pdev);
	spin_lock_init(&ufdev->lock);
	BUILD_BUG_ON(sizeof(ufdev->name) != sizeof(ufdev->netdev->name));
	strcpy(ufdev->name, ufdev->netdev->name);

	return ufdev;
}

void usnic_fwd_dev_free(struct usnic_fwd_dev *ufdev)
{
	kfree(ufdev);
}

void usnic_fwd_set_mac(struct usnic_fwd_dev *ufdev, char mac[ETH_ALEN])
{
	spin_lock(&ufdev->lock);
	memcpy(&ufdev->mac, mac, sizeof(ufdev->mac));
	spin_unlock(&ufdev->lock);
}

void usnic_fwd_add_ipaddr(struct usnic_fwd_dev *ufdev, __be32 inaddr)
{
	spin_lock(&ufdev->lock);
	if (!ufdev->inaddr)
		ufdev->inaddr = inaddr;
	spin_unlock(&ufdev->lock);
}

void usnic_fwd_del_ipaddr(struct usnic_fwd_dev *ufdev)
{
	spin_lock(&ufdev->lock);
	ufdev->inaddr = 0;
	spin_unlock(&ufdev->lock);
}

void usnic_fwd_carrier_up(struct usnic_fwd_dev *ufdev)
{
	spin_lock(&ufdev->lock);
	ufdev->link_up = 1;
	spin_unlock(&ufdev->lock);
}

void usnic_fwd_carrier_down(struct usnic_fwd_dev *ufdev)
{
	spin_lock(&ufdev->lock);
	ufdev->link_up = 0;
	spin_unlock(&ufdev->lock);
}

void usnic_fwd_set_mtu(struct usnic_fwd_dev *ufdev, unsigned int mtu)
{
	spin_lock(&ufdev->lock);
	ufdev->mtu = mtu;
	spin_unlock(&ufdev->lock);
}

static int usnic_fwd_dev_ready_locked(struct usnic_fwd_dev *ufdev)
{
	lockdep_assert_held(&ufdev->lock);

	if (!ufdev->link_up)
		return -EPERM;

	return 0;
}

static int validate_filter_locked(struct usnic_fwd_dev *ufdev,
					struct filter *filter)
{

	lockdep_assert_held(&ufdev->lock);

	if (filter->type == FILTER_IPV4_5TUPLE) {
		if (!(filter->u.ipv4.flags & FILTER_FIELD_5TUP_DST_AD))
			return -EACCES;
		if (!(filter->u.ipv4.flags & FILTER_FIELD_5TUP_DST_PT))
			return -EBUSY;
		else if (ufdev->inaddr == 0)
			return -EINVAL;
		else if (filter->u.ipv4.dst_port == 0)
			return -ERANGE;
		else if (ntohl(ufdev->inaddr) != filter->u.ipv4.dst_addr)
			return -EFAULT;
		else
			return 0;
	}

	return 0;
}

static void fill_tlv(struct filter_tlv *tlv, struct filter *filter,
		struct filter_action *action)
{
	tlv->type = CLSF_TLV_FILTER;
	tlv->length = sizeof(struct filter);
	*((struct filter *)&tlv->val) = *filter;

	tlv = (struct filter_tlv *)((char *)tlv + sizeof(struct filter_tlv) +
			sizeof(struct filter));
	tlv->type = CLSF_TLV_ACTION;
	tlv->length = sizeof(struct filter_action);
	*((struct filter_action *)&tlv->val) = *action;
}

struct usnic_fwd_flow*
usnic_fwd_alloc_flow(struct usnic_fwd_dev *ufdev, struct filter *filter,
				struct usnic_filter_action *uaction)
{
	struct filter_tlv *tlv;
	struct pci_dev *pdev;
	struct usnic_fwd_flow *flow;
	uint64_t a0, a1;
	uint64_t tlv_size;
	dma_addr_t tlv_pa;
	int status;

	pdev = ufdev->pdev;
	tlv_size = (2*sizeof(struct filter_tlv) + sizeof(struct filter) +
			sizeof(struct filter_action));

	flow = kzalloc(sizeof(*flow), GFP_ATOMIC);
	if (!flow)
		return ERR_PTR(-ENOMEM);

	tlv = dma_alloc_coherent(&pdev->dev, tlv_size, &tlv_pa, GFP_ATOMIC);
	if (!tlv) {
		usnic_err("Failed to allocate memory\n");
		status = -ENOMEM;
		goto out_free_flow;
	}

	fill_tlv(tlv, filter, &uaction->action);

	spin_lock(&ufdev->lock);
	status = usnic_fwd_dev_ready_locked(ufdev);
	if (status) {
		usnic_err("Forwarding dev %s not ready with status %d\n",
				ufdev->name, status);
		goto out_free_tlv;
	}

	status = validate_filter_locked(ufdev, filter);
	if (status) {
		usnic_err("Failed to validate filter with status %d\n",
				status);
		goto out_free_tlv;
	}

	/* Issue Devcmd */
	a0 = tlv_pa;
	a1 = tlv_size;
	status = usnic_fwd_devcmd_locked(ufdev, uaction->vnic_idx,
						CMD_ADD_FILTER, &a0, &a1);
	if (status) {
		usnic_err("VF %s Filter add failed with status:%d",
				ufdev->name, status);
		status = -EFAULT;
		goto out_free_tlv;
	} else {
		usnic_dbg("VF %s FILTER ID:%llu", ufdev->name, a0);
	}

	flow->flow_id = (uint32_t) a0;
	flow->vnic_idx = uaction->vnic_idx;
	flow->ufdev = ufdev;

out_free_tlv:
	spin_unlock(&ufdev->lock);
	dma_free_coherent(&pdev->dev, tlv_size, tlv, tlv_pa);
	if (!status)
		return flow;
out_free_flow:
	kfree(flow);
	return ERR_PTR(status);
}

int usnic_fwd_dealloc_flow(struct usnic_fwd_flow *flow)
{
	int status;
	u64 a0, a1;

	a0 = flow->flow_id;

	status = usnic_fwd_devcmd(flow->ufdev, flow->vnic_idx,
					CMD_DEL_FILTER, &a0, &a1);
	if (status) {
		if (status == ERR_EINVAL) {
			usnic_dbg("Filter %u already deleted for VF Idx %u pf: %s status: %d",
					flow->flow_id, flow->vnic_idx,
					flow->ufdev->name, status);
		} else {
			usnic_err("PF %s VF Idx %u Filter: %u FILTER DELETE failed with status %d",
					flow->ufdev->name, flow->vnic_idx,
					flow->flow_id, status);
		}
		status = 0;
		/*
		 * Log the error and fake success to the caller because if
		 * a flow fails to be deleted in the firmware, it is an
		 * unrecoverable error.
		 */
	} else {
		usnic_dbg("PF %s VF Idx %u Filter: %u FILTER DELETED",
				flow->ufdev->name, flow->vnic_idx,
				flow->flow_id);
	}

	kfree(flow);
	return status;
}

int usnic_fwd_enable_qp(struct usnic_fwd_dev *ufdev, int vnic_idx, int qp_idx)
{
	int status;
	struct net_device *pf_netdev;
	u64 a0, a1;

	pf_netdev = ufdev->netdev;
	a0 = qp_idx;
	a1 = CMD_QP_RQWQ;

	status = usnic_fwd_devcmd(ufdev, vnic_idx, CMD_QP_ENABLE,
						&a0, &a1);
	if (status) {
		usnic_err("PF %s VNIC Index %u RQ Index: %u ENABLE Failed with status %d",
				netdev_name(pf_netdev),
				vnic_idx,
				qp_idx,
				status);
	} else {
		usnic_dbg("PF %s VNIC Index %u RQ Index: %u ENABLED",
				netdev_name(pf_netdev),
				vnic_idx, qp_idx);
	}

	return status;
}

int usnic_fwd_disable_qp(struct usnic_fwd_dev *ufdev, int vnic_idx, int qp_idx)
{
	int status;
	u64 a0, a1;
	struct net_device *pf_netdev;

	pf_netdev = ufdev->netdev;
	a0 = qp_idx;
	a1 = CMD_QP_RQWQ;

	status = usnic_fwd_devcmd(ufdev, vnic_idx, CMD_QP_DISABLE,
			&a0, &a1);
	if (status) {
		usnic_err("PF %s VNIC Index %u RQ Index: %u DISABLE Failed with status %d",
				netdev_name(pf_netdev),
				vnic_idx,
				qp_idx,
				status);
	} else {
		usnic_dbg("PF %s VNIC Index %u RQ Index: %u DISABLED",
				netdev_name(pf_netdev),
				vnic_idx,
				qp_idx);
	}

	return status;
}
