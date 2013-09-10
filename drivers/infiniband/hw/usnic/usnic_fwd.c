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
#include <linux/netdevice.h>
#include <linux/pci.h>

#include "enic_api.h"
#include "usnic_common_pkt_hdr.h"
#include "usnic_fwd.h"
#include "usnic_log.h"

struct usnic_fwd_dev *usnic_fwd_dev_alloc(struct pci_dev *pdev)
{
	struct usnic_fwd_dev *ufdev;

	ufdev = kzalloc(sizeof(*ufdev), GFP_KERNEL);
	if (!ufdev)
		return NULL;

	ufdev->pdev = pdev;
	ufdev->netdev = pci_get_drvdata(pdev);
	spin_lock_init(&ufdev->lock);

	return ufdev;
}

void usnic_fwd_dev_free(struct usnic_fwd_dev *ufdev)
{
	kfree(ufdev);
}

static int usnic_fwd_devcmd(struct usnic_fwd_dev *ufdev, int vnic_idx,
				enum vnic_devcmd_cmd cmd, u64 *a0, u64 *a1)
{
	int status;
	struct net_device *netdev = ufdev->netdev;

	spin_lock(&ufdev->lock);
	status = enic_api_devcmd_proxy_by_index(netdev,
			vnic_idx,
			cmd,
			a0, a1,
			1000);
	spin_unlock(&ufdev->lock);
	if (status) {
		if (status == ERR_EINVAL && cmd == CMD_DEL_FILTER) {
			usnic_dbg("Dev %s vnic idx %u cmd %u already deleted",
					netdev_name(netdev), vnic_idx, cmd);
		} else {
			usnic_err("Dev %s vnic idx %u cmd %u failed with status %d\n",
					netdev_name(netdev), vnic_idx, cmd,
					status);
		}
	} else {
		usnic_dbg("Dev %s vnic idx %u cmd %u success",
				netdev_name(netdev), vnic_idx,
				cmd);
	}

	return status;
}

int usnic_fwd_add_usnic_filter(struct usnic_fwd_dev *ufdev, int vnic_idx,
				int rq_idx, struct usnic_fwd_filter *fwd_filter,
				struct usnic_fwd_filter_hndl **filter_hndl)
{
	struct filter_tlv *tlv, *tlv_va;
	struct filter *filter;
	struct filter_action *action;
	struct pci_dev *pdev;
	struct usnic_fwd_filter_hndl *usnic_filter_hndl;
	int status;
	u64 a0, a1;
	u64 tlv_size;
	dma_addr_t tlv_pa;

	pdev = ufdev->pdev;
	tlv_size = (2*sizeof(struct filter_tlv) +
		sizeof(struct filter) +
		sizeof(struct filter_action));
	tlv = pci_alloc_consistent(pdev, tlv_size, &tlv_pa);
	if (!tlv) {
		usnic_err("Failed to allocate memory\n");
		return -ENOMEM;
	}

	usnic_filter_hndl = kzalloc(sizeof(*usnic_filter_hndl), GFP_ATOMIC);
	if (!usnic_filter_hndl) {
		usnic_err("Failed to allocate memory for hndl\n");
		pci_free_consistent(pdev, tlv_size, tlv, tlv_pa);
		return -ENOMEM;
	}

	tlv_va = tlv;
	a0 = tlv_pa;
	a1 = tlv_size;
	memset(tlv, 0, tlv_size);
	tlv->type = CLSF_TLV_FILTER;
	tlv->length = sizeof(struct filter);
	filter = (struct filter *)&tlv->val;
	filter->type = FILTER_USNIC_ID;
	filter->u.usnic.ethtype = USNIC_ROCE_ETHERTYPE;
	filter->u.usnic.flags = FILTER_FIELD_USNIC_ETHTYPE |
					FILTER_FIELD_USNIC_ID |
					FILTER_FIELD_USNIC_PROTO;
	filter->u.usnic.proto_version = (USNIC_ROCE_GRH_VER <<
						USNIC_ROCE_GRH_VER_SHIFT)
							| USNIC_PROTO_VER;
	filter->u.usnic.usnic_id = fwd_filter->port_num;
	tlv = (struct filter_tlv *)((char *)tlv + sizeof(struct filter_tlv) +
			sizeof(struct filter));
	tlv->type = CLSF_TLV_ACTION;
	tlv->length = sizeof(struct filter_action);
	action = (struct filter_action *)&tlv->val;
	action->type = FILTER_ACTION_RQ_STEERING;
	action->u.rq_idx = rq_idx;

	status = usnic_fwd_devcmd(ufdev, vnic_idx, CMD_ADD_FILTER, &a0, &a1);
	pci_free_consistent(pdev, tlv_size, tlv_va, tlv_pa);
	if (status) {
		usnic_err("VF %s Filter add failed with status:%d",
				pci_name(pdev),
				status);
		kfree(usnic_filter_hndl);
		return status;
	} else {
		usnic_dbg("VF %s FILTER ID:%u",
				pci_name(pdev),
				(u32)a0);
	}

	usnic_filter_hndl->type = FILTER_USNIC_ID;
	usnic_filter_hndl->id = (u32)a0;
	usnic_filter_hndl->vnic_idx = vnic_idx;
	usnic_filter_hndl->ufdev = ufdev;
	usnic_filter_hndl->filter = fwd_filter;
	*filter_hndl = usnic_filter_hndl;

	return status;
}

int usnic_fwd_del_filter(struct usnic_fwd_filter_hndl *filter_hndl)
{
	int status;
	u64 a0, a1;
	struct net_device *netdev;

	netdev = filter_hndl->ufdev->netdev;
	a0 = filter_hndl->id;

	status = usnic_fwd_devcmd(filter_hndl->ufdev, filter_hndl->vnic_idx,
					CMD_DEL_FILTER, &a0, &a1);
	if (status) {
		if (status == ERR_EINVAL) {
			usnic_dbg("Filter %u already deleted for VF Idx %u pf: %s status: %d",
					filter_hndl->id, filter_hndl->vnic_idx,
					netdev_name(netdev), status);
			status = 0;
			kfree(filter_hndl);
		} else {
			usnic_err("PF %s VF Idx %u Filter: %u FILTER DELETE failed with status %d",
					netdev_name(netdev),
					filter_hndl->vnic_idx, filter_hndl->id,
					status);
		}
	} else {
		usnic_dbg("PF %s VF Idx %u Filter: %u FILTER DELETED",
				netdev_name(netdev), filter_hndl->vnic_idx,
				filter_hndl->id);
		kfree(filter_hndl);
	}

	return status;
}

int usnic_fwd_enable_rq(struct usnic_fwd_dev *ufdev, int vnic_idx, int rq_idx)
{
	int status;
	struct net_device *pf_netdev;
	u64 a0, a1;

	pf_netdev = ufdev->netdev;
	a0 = rq_idx;
	a1 = CMD_QP_RQWQ;

	status = usnic_fwd_devcmd(ufdev, vnic_idx, CMD_QP_ENABLE, &a0, &a1);

	if (status) {
		usnic_err("PF %s VNIC Index %u RQ Index: %u ENABLE Failed with status %d",
				netdev_name(pf_netdev),
				vnic_idx,
				rq_idx,
				status);
	} else {
		usnic_dbg("PF %s VNIC Index %u RQ Index: %u ENABLED",
				netdev_name(pf_netdev),
				vnic_idx, rq_idx);
	}

	return status;
}

int usnic_fwd_disable_rq(struct usnic_fwd_dev *ufdev, int vnic_idx, int rq_idx)
{
	int status;
	u64 a0, a1;
	struct net_device *pf_netdev;

	pf_netdev = ufdev->netdev;
	a0 = rq_idx;
	a1 = CMD_QP_RQWQ;

	status = usnic_fwd_devcmd(ufdev, vnic_idx, CMD_QP_DISABLE, &a0, &a1);

	if (status) {
		usnic_err("PF %s VNIC Index %u RQ Index: %u DISABLE Failed with status %d",
				netdev_name(pf_netdev),
				vnic_idx,
				rq_idx,
				status);
	} else {
		usnic_dbg("PF %s VNIC Index %u RQ Index: %u DISABLED",
				netdev_name(pf_netdev),
				vnic_idx,
				rq_idx);
	}

	return status;
}
