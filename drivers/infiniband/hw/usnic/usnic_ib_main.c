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
 * Author: Upinder Malhi <umalhi@cisco.com>
 * Author: Anant Deepak <anadeepa@cisco.com>
 * Author: Cesare Cantu' <cantuc@cisco.com>
 * Author: Jeff Squyres <jsquyres@cisco.com>
 * Author: Kiran Thirumalai <kithirum@cisco.com>
 * Author: Xuyang Wang <xuywang@cisco.com>
 * Author: Reese Faucette <rfaucett@cisco.com>
 *
 */

#include <linux/module.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/netdevice.h>

#include <rdma/ib_user_verbs.h>
#include <rdma/ib_addr.h>

#include "usnic_abi.h"
#include "usnic_common_util.h"
#include "usnic_ib.h"
#include "usnic_ib_qp_grp.h"
#include "usnic_log.h"
#include "usnic_fwd.h"
#include "usnic_debugfs.h"
#include "usnic_ib_verbs.h"
#include "usnic_transport.h"
#include "usnic_uiom.h"
#include "usnic_ib_sysfs.h"

unsigned int usnic_log_lvl = USNIC_LOG_LVL_ERR;
unsigned int usnic_ib_share_vf = 1;

static const char usnic_version[] =
	DRV_NAME ": Cisco VIC (USNIC) Verbs Driver v"
	DRV_VERSION " (" DRV_RELDATE ")\n";

static DEFINE_MUTEX(usnic_ib_ibdev_list_lock);
static LIST_HEAD(usnic_ib_ibdev_list);

/* Callback dump funcs */
static int usnic_ib_dump_vf_hdr(void *obj, char *buf, int buf_sz)
{
	struct usnic_ib_vf *vf = obj;
	return scnprintf(buf, buf_sz, "PF: %s ", dev_name(&vf->pf->ib_dev.dev));
}
/* End callback dump funcs */

static void usnic_ib_dump_vf(struct usnic_ib_vf *vf, char *buf, int buf_sz)
{
	usnic_vnic_dump(vf->vnic, buf, buf_sz, vf,
			usnic_ib_dump_vf_hdr,
			usnic_ib_qp_grp_dump_hdr, usnic_ib_qp_grp_dump_rows);
}

void usnic_ib_log_vf(struct usnic_ib_vf *vf)
{
	char *buf = kzalloc(1000, GFP_KERNEL);

	if (!buf)
		return;

	usnic_ib_dump_vf(vf, buf, 1000);
	usnic_dbg("%s\n", buf);

	kfree(buf);
}

/* Start of netdev section */
static void usnic_ib_qp_grp_modify_active_to_err(struct usnic_ib_dev *us_ibdev)
{
	struct usnic_ib_ucontext *ctx;
	struct usnic_ib_qp_grp *qp_grp;
	enum ib_qp_state cur_state;
	int status;

	BUG_ON(!mutex_is_locked(&us_ibdev->usdev_lock));

	list_for_each_entry(ctx, &us_ibdev->ctx_list, link) {
		list_for_each_entry(qp_grp, &ctx->qp_grp_list, link) {
			cur_state = qp_grp->state;
			if (cur_state == IB_QPS_INIT ||
				cur_state == IB_QPS_RTR ||
				cur_state == IB_QPS_RTS) {
				status = usnic_ib_qp_grp_modify(qp_grp,
								IB_QPS_ERR,
								NULL);
				if (status) {
					usnic_err("Failed to transistion qp grp %u from %s to %s\n",
						qp_grp->grp_id,
						usnic_ib_qp_grp_state_to_string
						(cur_state),
						usnic_ib_qp_grp_state_to_string
						(IB_QPS_ERR));
				}
			}
		}
	}
}

static void usnic_ib_handle_usdev_event(struct usnic_ib_dev *us_ibdev,
					unsigned long event)
{
	struct net_device *netdev;
	struct ib_event ib_event;

	memset(&ib_event, 0, sizeof(ib_event));

	mutex_lock(&us_ibdev->usdev_lock);
	netdev = us_ibdev->netdev;
	switch (event) {
	case NETDEV_REBOOT:
		usnic_info("PF Reset on %s\n", dev_name(&us_ibdev->ib_dev.dev));
		usnic_ib_qp_grp_modify_active_to_err(us_ibdev);
		ib_event.event = IB_EVENT_PORT_ERR;
		ib_event.device = &us_ibdev->ib_dev;
		ib_event.element.port_num = 1;
		ib_dispatch_event(&ib_event);
		break;
	case NETDEV_UP:
	case NETDEV_DOWN:
	case NETDEV_CHANGE:
		if (!us_ibdev->ufdev->link_up &&
				netif_carrier_ok(netdev)) {
			usnic_fwd_carrier_up(us_ibdev->ufdev);
			usnic_info("Link UP on %s\n",
				   dev_name(&us_ibdev->ib_dev.dev));
			ib_event.event = IB_EVENT_PORT_ACTIVE;
			ib_event.device = &us_ibdev->ib_dev;
			ib_event.element.port_num = 1;
			ib_dispatch_event(&ib_event);
		} else if (us_ibdev->ufdev->link_up &&
				!netif_carrier_ok(netdev)) {
			usnic_fwd_carrier_down(us_ibdev->ufdev);
			usnic_info("Link DOWN on %s\n",
				   dev_name(&us_ibdev->ib_dev.dev));
			usnic_ib_qp_grp_modify_active_to_err(us_ibdev);
			ib_event.event = IB_EVENT_PORT_ERR;
			ib_event.device = &us_ibdev->ib_dev;
			ib_event.element.port_num = 1;
			ib_dispatch_event(&ib_event);
		} else {
			usnic_dbg("Ignoring %s on %s\n",
					netdev_cmd_to_name(event),
					dev_name(&us_ibdev->ib_dev.dev));
		}
		break;
	case NETDEV_CHANGEADDR:
		if (!memcmp(us_ibdev->ufdev->mac, netdev->dev_addr,
				sizeof(us_ibdev->ufdev->mac))) {
			usnic_dbg("Ignoring addr change on %s\n",
				  dev_name(&us_ibdev->ib_dev.dev));
		} else {
			usnic_info(" %s old mac: %pM new mac: %pM\n",
					dev_name(&us_ibdev->ib_dev.dev),
					us_ibdev->ufdev->mac,
					netdev->dev_addr);
			usnic_fwd_set_mac(us_ibdev->ufdev, netdev->dev_addr);
			usnic_ib_qp_grp_modify_active_to_err(us_ibdev);
			ib_event.event = IB_EVENT_GID_CHANGE;
			ib_event.device = &us_ibdev->ib_dev;
			ib_event.element.port_num = 1;
			ib_dispatch_event(&ib_event);
		}

		break;
	case NETDEV_CHANGEMTU:
		if (us_ibdev->ufdev->mtu != netdev->mtu) {
			usnic_info("MTU Change on %s old: %u new: %u\n",
					dev_name(&us_ibdev->ib_dev.dev),
					us_ibdev->ufdev->mtu, netdev->mtu);
			usnic_fwd_set_mtu(us_ibdev->ufdev, netdev->mtu);
			usnic_ib_qp_grp_modify_active_to_err(us_ibdev);
		} else {
			usnic_dbg("Ignoring MTU change on %s\n",
				  dev_name(&us_ibdev->ib_dev.dev));
		}
		break;
	default:
		usnic_dbg("Ignoring event %s on %s",
				netdev_cmd_to_name(event),
				dev_name(&us_ibdev->ib_dev.dev));
	}
	mutex_unlock(&us_ibdev->usdev_lock);
}

static int usnic_ib_netdevice_event(struct notifier_block *notifier,
					unsigned long event, void *ptr)
{
	struct usnic_ib_dev *us_ibdev;
	struct ib_device *ibdev;

	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);

	ibdev = ib_device_get_by_netdev(netdev, RDMA_DRIVER_USNIC);
	if (!ibdev)
		return NOTIFY_DONE;

	us_ibdev = container_of(ibdev, struct usnic_ib_dev, ib_dev);
	usnic_ib_handle_usdev_event(us_ibdev, event);
	ib_device_put(ibdev);
	return NOTIFY_DONE;
}

static struct notifier_block usnic_ib_netdevice_notifier = {
	.notifier_call = usnic_ib_netdevice_event
};
/* End of netdev section */

/* Start of inet section */
static int usnic_ib_handle_inet_event(struct usnic_ib_dev *us_ibdev,
					unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = ptr;
	struct ib_event ib_event;

	mutex_lock(&us_ibdev->usdev_lock);

	switch (event) {
	case NETDEV_DOWN:
		usnic_info("%s via ip notifiers",
				netdev_cmd_to_name(event));
		usnic_fwd_del_ipaddr(us_ibdev->ufdev);
		usnic_ib_qp_grp_modify_active_to_err(us_ibdev);
		ib_event.event = IB_EVENT_GID_CHANGE;
		ib_event.device = &us_ibdev->ib_dev;
		ib_event.element.port_num = 1;
		ib_dispatch_event(&ib_event);
		break;
	case NETDEV_UP:
		usnic_fwd_add_ipaddr(us_ibdev->ufdev, ifa->ifa_address);
		usnic_info("%s via ip notifiers: ip %pI4",
				netdev_cmd_to_name(event),
				&us_ibdev->ufdev->inaddr);
		ib_event.event = IB_EVENT_GID_CHANGE;
		ib_event.device = &us_ibdev->ib_dev;
		ib_event.element.port_num = 1;
		ib_dispatch_event(&ib_event);
		break;
	default:
		usnic_info("Ignoring event %s on %s",
				netdev_cmd_to_name(event),
				dev_name(&us_ibdev->ib_dev.dev));
	}
	mutex_unlock(&us_ibdev->usdev_lock);

	return NOTIFY_DONE;
}

static int usnic_ib_inetaddr_event(struct notifier_block *notifier,
					unsigned long event, void *ptr)
{
	struct usnic_ib_dev *us_ibdev;
	struct in_ifaddr *ifa = ptr;
	struct net_device *netdev = ifa->ifa_dev->dev;
	struct ib_device *ibdev;

	ibdev = ib_device_get_by_netdev(netdev, RDMA_DRIVER_USNIC);
	if (!ibdev)
		return NOTIFY_DONE;

	us_ibdev = container_of(ibdev, struct usnic_ib_dev, ib_dev);
	usnic_ib_handle_inet_event(us_ibdev, event, ptr);
	ib_device_put(ibdev);
	return NOTIFY_DONE;
}
static struct notifier_block usnic_ib_inetaddr_notifier = {
	.notifier_call = usnic_ib_inetaddr_event
};
/* End of inet section*/

static int usnic_port_immutable(struct ib_device *ibdev, u8 port_num,
			        struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	immutable->core_cap_flags = RDMA_CORE_PORT_USNIC;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;

	return 0;
}

static void usnic_get_dev_fw_str(struct ib_device *device, char *str)
{
	struct usnic_ib_dev *us_ibdev =
		container_of(device, struct usnic_ib_dev, ib_dev);
	struct ethtool_drvinfo info;

	mutex_lock(&us_ibdev->usdev_lock);
	us_ibdev->netdev->ethtool_ops->get_drvinfo(us_ibdev->netdev, &info);
	mutex_unlock(&us_ibdev->usdev_lock);

	snprintf(str, IB_FW_VERSION_NAME_MAX, "%s", info.fw_version);
}

static const struct ib_device_ops usnic_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_USNIC,
	.uverbs_abi_ver = USNIC_UVERBS_ABI_VERSION,

	.alloc_pd = usnic_ib_alloc_pd,
	.alloc_ucontext = usnic_ib_alloc_ucontext,
	.create_cq = usnic_ib_create_cq,
	.create_qp = usnic_ib_create_qp,
	.dealloc_pd = usnic_ib_dealloc_pd,
	.dealloc_ucontext = usnic_ib_dealloc_ucontext,
	.dereg_mr = usnic_ib_dereg_mr,
	.destroy_cq = usnic_ib_destroy_cq,
	.destroy_qp = usnic_ib_destroy_qp,
	.get_dev_fw_str = usnic_get_dev_fw_str,
	.get_link_layer = usnic_ib_port_link_layer,
	.get_port_immutable = usnic_port_immutable,
	.mmap = usnic_ib_mmap,
	.modify_qp = usnic_ib_modify_qp,
	.query_device = usnic_ib_query_device,
	.query_gid = usnic_ib_query_gid,
	.query_pkey = usnic_ib_query_pkey,
	.query_port = usnic_ib_query_port,
	.query_qp = usnic_ib_query_qp,
	.reg_user_mr = usnic_ib_reg_mr,
	INIT_RDMA_OBJ_SIZE(ib_pd, usnic_ib_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_cq, usnic_ib_cq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, usnic_ib_ucontext, ibucontext),
};

/* Start of PF discovery section */
static void *usnic_ib_device_add(struct pci_dev *dev)
{
	struct usnic_ib_dev *us_ibdev;
	union ib_gid gid;
	struct in_device *ind;
	struct net_device *netdev;
	int ret;

	usnic_dbg("\n");
	netdev = pci_get_drvdata(dev);

	us_ibdev = ib_alloc_device(usnic_ib_dev, ib_dev);
	if (!us_ibdev) {
		usnic_err("Device %s context alloc failed\n",
				netdev_name(pci_get_drvdata(dev)));
		return ERR_PTR(-EFAULT);
	}

	us_ibdev->ufdev = usnic_fwd_dev_alloc(dev);
	if (!us_ibdev->ufdev) {
		usnic_err("Failed to alloc ufdev for %s\n", pci_name(dev));
		goto err_dealloc;
	}

	mutex_init(&us_ibdev->usdev_lock);
	INIT_LIST_HEAD(&us_ibdev->vf_dev_list);
	INIT_LIST_HEAD(&us_ibdev->ctx_list);

	us_ibdev->pdev = dev;
	us_ibdev->netdev = pci_get_drvdata(dev);
	us_ibdev->ib_dev.node_type = RDMA_NODE_USNIC_UDP;
	us_ibdev->ib_dev.phys_port_cnt = USNIC_IB_PORT_CNT;
	us_ibdev->ib_dev.num_comp_vectors = USNIC_IB_NUM_COMP_VECTORS;
	us_ibdev->ib_dev.dev.parent = &dev->dev;

	us_ibdev->ib_dev.uverbs_cmd_mask =
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT) |
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE) |
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT) |
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD) |
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD) |
		(1ull << IB_USER_VERBS_CMD_REG_MR) |
		(1ull << IB_USER_VERBS_CMD_DEREG_MR) |
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ) |
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ) |
		(1ull << IB_USER_VERBS_CMD_CREATE_QP) |
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP) |
		(1ull << IB_USER_VERBS_CMD_QUERY_QP) |
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP) |
		(1ull << IB_USER_VERBS_CMD_ATTACH_MCAST) |
		(1ull << IB_USER_VERBS_CMD_DETACH_MCAST) |
		(1ull << IB_USER_VERBS_CMD_OPEN_QP);

	ib_set_device_ops(&us_ibdev->ib_dev, &usnic_dev_ops);

	rdma_set_device_sysfs_group(&us_ibdev->ib_dev, &usnic_attr_group);

	ret = ib_device_set_netdev(&us_ibdev->ib_dev, us_ibdev->netdev, 1);
	if (ret)
		goto err_fwd_dealloc;

	if (ib_register_device(&us_ibdev->ib_dev, "usnic_%d"))
		goto err_fwd_dealloc;

	usnic_fwd_set_mtu(us_ibdev->ufdev, us_ibdev->netdev->mtu);
	usnic_fwd_set_mac(us_ibdev->ufdev, us_ibdev->netdev->dev_addr);
	if (netif_carrier_ok(us_ibdev->netdev))
		usnic_fwd_carrier_up(us_ibdev->ufdev);

	rcu_read_lock();
	ind = __in_dev_get_rcu(netdev);
	if (ind) {
		const struct in_ifaddr *ifa;

		ifa = rcu_dereference(ind->ifa_list);
		if (ifa)
			usnic_fwd_add_ipaddr(us_ibdev->ufdev, ifa->ifa_address);
	}
	rcu_read_unlock();

	usnic_mac_ip_to_gid(us_ibdev->netdev->perm_addr,
				us_ibdev->ufdev->inaddr, &gid.raw[0]);
	memcpy(&us_ibdev->ib_dev.node_guid, &gid.global.interface_id,
		sizeof(gid.global.interface_id));
	kref_init(&us_ibdev->vf_cnt);

	usnic_info("Added ibdev: %s netdev: %s with mac %pM Link: %u MTU: %u\n",
		   dev_name(&us_ibdev->ib_dev.dev),
		   netdev_name(us_ibdev->netdev), us_ibdev->ufdev->mac,
		   us_ibdev->ufdev->link_up, us_ibdev->ufdev->mtu);
	return us_ibdev;

err_fwd_dealloc:
	usnic_fwd_dev_free(us_ibdev->ufdev);
err_dealloc:
	usnic_err("failed -- deallocing device\n");
	ib_dealloc_device(&us_ibdev->ib_dev);
	return NULL;
}

static void usnic_ib_device_remove(struct usnic_ib_dev *us_ibdev)
{
	usnic_info("Unregistering %s\n", dev_name(&us_ibdev->ib_dev.dev));
	usnic_ib_sysfs_unregister_usdev(us_ibdev);
	usnic_fwd_dev_free(us_ibdev->ufdev);
	ib_unregister_device(&us_ibdev->ib_dev);
	ib_dealloc_device(&us_ibdev->ib_dev);
}

static void usnic_ib_undiscover_pf(struct kref *kref)
{
	struct usnic_ib_dev *us_ibdev, *tmp;
	struct pci_dev *dev;
	bool found = false;

	dev = container_of(kref, struct usnic_ib_dev, vf_cnt)->pdev;
	mutex_lock(&usnic_ib_ibdev_list_lock);
	list_for_each_entry_safe(us_ibdev, tmp,
				&usnic_ib_ibdev_list, ib_dev_link) {
		if (us_ibdev->pdev == dev) {
			list_del(&us_ibdev->ib_dev_link);
			found = true;
			break;
		}
	}


	mutex_unlock(&usnic_ib_ibdev_list_lock);
	if (found)
		usnic_ib_device_remove(us_ibdev);
	else
		WARN(1, "Failed to remove PF %s\n", pci_name(dev));
}

static struct usnic_ib_dev *usnic_ib_discover_pf(struct usnic_vnic *vnic)
{
	struct usnic_ib_dev *us_ibdev;
	struct pci_dev *parent_pci, *vf_pci;
	int err;

	vf_pci = usnic_vnic_get_pdev(vnic);
	parent_pci = pci_physfn(vf_pci);

	BUG_ON(!parent_pci);

	mutex_lock(&usnic_ib_ibdev_list_lock);
	list_for_each_entry(us_ibdev, &usnic_ib_ibdev_list, ib_dev_link) {
		if (us_ibdev->pdev == parent_pci) {
			kref_get(&us_ibdev->vf_cnt);
			goto out;
		}
	}

	us_ibdev = usnic_ib_device_add(parent_pci);
	if (IS_ERR_OR_NULL(us_ibdev)) {
		us_ibdev = us_ibdev ? us_ibdev : ERR_PTR(-EFAULT);
		goto out;
	}

	err = usnic_ib_sysfs_register_usdev(us_ibdev);
	if (err) {
		usnic_ib_device_remove(us_ibdev);
		us_ibdev = ERR_PTR(err);
		goto out;
	}

	list_add(&us_ibdev->ib_dev_link, &usnic_ib_ibdev_list);
out:
	mutex_unlock(&usnic_ib_ibdev_list_lock);
	return us_ibdev;
}
/* End of PF discovery section */

/* Start of PCI section */

static const struct pci_device_id usnic_ib_pci_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_CISCO, PCI_DEVICE_ID_CISCO_VIC_USPACE_NIC)},
	{0,}
};

static int usnic_ib_pci_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	int err;
	struct usnic_ib_dev *pf;
	struct usnic_ib_vf *vf;
	enum usnic_vnic_res_type res_type;

	vf = kzalloc(sizeof(*vf), GFP_KERNEL);
	if (!vf)
		return -ENOMEM;

	err = pci_enable_device(pdev);
	if (err) {
		usnic_err("Failed to enable %s with err %d\n",
				pci_name(pdev), err);
		goto out_clean_vf;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		usnic_err("Failed to request region for %s with err %d\n",
				pci_name(pdev), err);
		goto out_disable_device;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, vf);

	vf->vnic = usnic_vnic_alloc(pdev);
	if (IS_ERR_OR_NULL(vf->vnic)) {
		err = vf->vnic ? PTR_ERR(vf->vnic) : -ENOMEM;
		usnic_err("Failed to alloc vnic for %s with err %d\n",
				pci_name(pdev), err);
		goto out_release_regions;
	}

	pf = usnic_ib_discover_pf(vf->vnic);
	if (IS_ERR_OR_NULL(pf)) {
		usnic_err("Failed to discover pf of vnic %s with err%ld\n",
				pci_name(pdev), PTR_ERR(pf));
		err = pf ? PTR_ERR(pf) : -EFAULT;
		goto out_clean_vnic;
	}

	vf->pf = pf;
	spin_lock_init(&vf->lock);
	mutex_lock(&pf->usdev_lock);
	list_add_tail(&vf->link, &pf->vf_dev_list);
	/*
	 * Save max settings (will be same for each VF, easier to re-write than
	 * to say "if (!set) { set_values(); set=1; }
	 */
	for (res_type = USNIC_VNIC_RES_TYPE_EOL+1;
			res_type < USNIC_VNIC_RES_TYPE_MAX;
			res_type++) {
		pf->vf_res_cnt[res_type] = usnic_vnic_res_cnt(vf->vnic,
								res_type);
	}

	mutex_unlock(&pf->usdev_lock);

	usnic_info("Registering usnic VF %s into PF %s\n", pci_name(pdev),
		   dev_name(&pf->ib_dev.dev));
	usnic_ib_log_vf(vf);
	return 0;

out_clean_vnic:
	usnic_vnic_free(vf->vnic);
out_release_regions:
	pci_set_drvdata(pdev, NULL);
	pci_clear_master(pdev);
	pci_release_regions(pdev);
out_disable_device:
	pci_disable_device(pdev);
out_clean_vf:
	kfree(vf);
	return err;
}

static void usnic_ib_pci_remove(struct pci_dev *pdev)
{
	struct usnic_ib_vf *vf = pci_get_drvdata(pdev);
	struct usnic_ib_dev *pf = vf->pf;

	mutex_lock(&pf->usdev_lock);
	list_del(&vf->link);
	mutex_unlock(&pf->usdev_lock);

	kref_put(&pf->vf_cnt, usnic_ib_undiscover_pf);
	usnic_vnic_free(vf->vnic);
	pci_set_drvdata(pdev, NULL);
	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(vf);

	usnic_info("Removed VF %s\n", pci_name(pdev));
}

/* PCI driver entry points */
static struct pci_driver usnic_ib_pci_driver = {
	.name = DRV_NAME,
	.id_table = usnic_ib_pci_ids,
	.probe = usnic_ib_pci_probe,
	.remove = usnic_ib_pci_remove,
};
/* End of PCI section */

/* Start of module section */
static int __init usnic_ib_init(void)
{
	int err;

	printk_once(KERN_INFO "%s", usnic_version);

	err = usnic_uiom_init(DRV_NAME);
	if (err) {
		usnic_err("Unable to initialize umem with err %d\n", err);
		return err;
	}

	err = pci_register_driver(&usnic_ib_pci_driver);
	if (err) {
		usnic_err("Unable to register with PCI\n");
		goto out_umem_fini;
	}

	err = register_netdevice_notifier(&usnic_ib_netdevice_notifier);
	if (err) {
		usnic_err("Failed to register netdev notifier\n");
		goto out_pci_unreg;
	}

	err = register_inetaddr_notifier(&usnic_ib_inetaddr_notifier);
	if (err) {
		usnic_err("Failed to register inet addr notifier\n");
		goto out_unreg_netdev_notifier;
	}

	err = usnic_transport_init();
	if (err) {
		usnic_err("Failed to initialize transport\n");
		goto out_unreg_inetaddr_notifier;
	}

	usnic_debugfs_init();

	return 0;

out_unreg_inetaddr_notifier:
	unregister_inetaddr_notifier(&usnic_ib_inetaddr_notifier);
out_unreg_netdev_notifier:
	unregister_netdevice_notifier(&usnic_ib_netdevice_notifier);
out_pci_unreg:
	pci_unregister_driver(&usnic_ib_pci_driver);
out_umem_fini:

	return err;
}

static void __exit usnic_ib_destroy(void)
{
	usnic_dbg("\n");
	usnic_debugfs_exit();
	usnic_transport_fini();
	unregister_inetaddr_notifier(&usnic_ib_inetaddr_notifier);
	unregister_netdevice_notifier(&usnic_ib_netdevice_notifier);
	pci_unregister_driver(&usnic_ib_pci_driver);
}

MODULE_DESCRIPTION("Cisco VIC (usNIC) Verbs Driver");
MODULE_AUTHOR("Upinder Malhi <umalhi@cisco.com>");
MODULE_LICENSE("Dual BSD/GPL");
module_param(usnic_log_lvl, uint, S_IRUGO | S_IWUSR);
module_param(usnic_ib_share_vf, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(usnic_log_lvl, " Off=0, Err=1, Info=2, Debug=3");
MODULE_PARM_DESC(usnic_ib_share_vf, "Off=0, On=1 VF sharing amongst QPs");
MODULE_DEVICE_TABLE(pci, usnic_ib_pci_ids);

module_init(usnic_ib_init);
module_exit(usnic_ib_destroy);
/* End of module section */
