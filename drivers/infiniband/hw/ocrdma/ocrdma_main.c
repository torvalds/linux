/*******************************************************************
 * This file is part of the Emulex RoCE Device Driver for          *
 * RoCE (RDMA over Converged Ethernet) adapters.                   *
 * Copyright (C) 2008-2012 Emulex. All rights reserved.            *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 *******************************************************************/

#include <linux/module.h>
#include <linux/idr.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_addr.h>

#include <linux/netdevice.h>
#include <net/addrconf.h>

#include "ocrdma.h"
#include "ocrdma_verbs.h"
#include "ocrdma_ah.h"
#include "be_roce.h"
#include "ocrdma_hw.h"
#include "ocrdma_stats.h"
#include "ocrdma_abi.h"

MODULE_VERSION(OCRDMA_ROCE_DRV_VERSION);
MODULE_DESCRIPTION(OCRDMA_ROCE_DRV_DESC " " OCRDMA_ROCE_DRV_VERSION);
MODULE_AUTHOR("Emulex Corporation");
MODULE_LICENSE("GPL");

static LIST_HEAD(ocrdma_dev_list);
static DEFINE_SPINLOCK(ocrdma_devlist_lock);
static DEFINE_IDR(ocrdma_dev_id);

static union ib_gid ocrdma_zero_sgid;

void ocrdma_get_guid(struct ocrdma_dev *dev, u8 *guid)
{
	u8 mac_addr[6];

	memcpy(&mac_addr[0], &dev->nic_info.mac_addr[0], ETH_ALEN);
	guid[0] = mac_addr[0] ^ 2;
	guid[1] = mac_addr[1];
	guid[2] = mac_addr[2];
	guid[3] = 0xff;
	guid[4] = 0xfe;
	guid[5] = mac_addr[3];
	guid[6] = mac_addr[4];
	guid[7] = mac_addr[5];
}

static bool ocrdma_add_sgid(struct ocrdma_dev *dev, union ib_gid *new_sgid)
{
	int i;
	unsigned long flags;

	memset(&ocrdma_zero_sgid, 0, sizeof(union ib_gid));


	spin_lock_irqsave(&dev->sgid_lock, flags);
	for (i = 0; i < OCRDMA_MAX_SGID; i++) {
		if (!memcmp(&dev->sgid_tbl[i], &ocrdma_zero_sgid,
			    sizeof(union ib_gid))) {
			/* found free entry */
			memcpy(&dev->sgid_tbl[i], new_sgid,
			       sizeof(union ib_gid));
			spin_unlock_irqrestore(&dev->sgid_lock, flags);
			return true;
		} else if (!memcmp(&dev->sgid_tbl[i], new_sgid,
				   sizeof(union ib_gid))) {
			/* entry already present, no addition is required. */
			spin_unlock_irqrestore(&dev->sgid_lock, flags);
			return false;
		}
	}
	spin_unlock_irqrestore(&dev->sgid_lock, flags);
	return false;
}

static bool ocrdma_del_sgid(struct ocrdma_dev *dev, union ib_gid *sgid)
{
	int found = false;
	int i;
	unsigned long flags;


	spin_lock_irqsave(&dev->sgid_lock, flags);
	/* first is default sgid, which cannot be deleted. */
	for (i = 1; i < OCRDMA_MAX_SGID; i++) {
		if (!memcmp(&dev->sgid_tbl[i], sgid, sizeof(union ib_gid))) {
			/* found matching entry */
			memset(&dev->sgid_tbl[i], 0, sizeof(union ib_gid));
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&dev->sgid_lock, flags);
	return found;
}

static int ocrdma_addr_event(unsigned long event, struct net_device *netdev,
			     union ib_gid *gid)
{
	struct ib_event gid_event;
	struct ocrdma_dev *dev;
	bool found = false;
	bool updated = false;
	bool is_vlan = false;

	is_vlan = netdev->priv_flags & IFF_802_1Q_VLAN;
	if (is_vlan)
		netdev = rdma_vlan_dev_real_dev(netdev);

	rcu_read_lock();
	list_for_each_entry_rcu(dev, &ocrdma_dev_list, entry) {
		if (dev->nic_info.netdev == netdev) {
			found = true;
			break;
		}
	}
	rcu_read_unlock();

	if (!found)
		return NOTIFY_DONE;

	mutex_lock(&dev->dev_lock);
	switch (event) {
	case NETDEV_UP:
		updated = ocrdma_add_sgid(dev, gid);
		break;
	case NETDEV_DOWN:
		updated = ocrdma_del_sgid(dev, gid);
		break;
	default:
		break;
	}
	if (updated) {
		/* GID table updated, notify the consumers about it */
		gid_event.device = &dev->ibdev;
		gid_event.element.port_num = 1;
		gid_event.event = IB_EVENT_GID_CHANGE;
		ib_dispatch_event(&gid_event);
	}
	mutex_unlock(&dev->dev_lock);
	return NOTIFY_OK;
}

static int ocrdma_inetaddr_event(struct notifier_block *notifier,
				  unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = ptr;
	union ib_gid gid;
	struct net_device *netdev = ifa->ifa_dev->dev;

	ipv6_addr_set_v4mapped(ifa->ifa_address, (struct in6_addr *)&gid);
	return ocrdma_addr_event(event, netdev, &gid);
}

static struct notifier_block ocrdma_inetaddr_notifier = {
	.notifier_call = ocrdma_inetaddr_event
};

#if IS_ENABLED(CONFIG_IPV6)

static int ocrdma_inet6addr_event(struct notifier_block *notifier,
				  unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)ptr;
	union  ib_gid *gid = (union ib_gid *)&ifa->addr;
	struct net_device *netdev = ifa->idev->dev;
	return ocrdma_addr_event(event, netdev, gid);
}

static struct notifier_block ocrdma_inet6addr_notifier = {
	.notifier_call = ocrdma_inet6addr_event
};

#endif /* IPV6 and VLAN */

static enum rdma_link_layer ocrdma_link_layer(struct ib_device *device,
					      u8 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

static int ocrdma_register_device(struct ocrdma_dev *dev)
{
	strlcpy(dev->ibdev.name, "ocrdma%d", IB_DEVICE_NAME_MAX);
	ocrdma_get_guid(dev, (u8 *)&dev->ibdev.node_guid);
	memcpy(dev->ibdev.node_desc, OCRDMA_NODE_DESC,
	       sizeof(OCRDMA_NODE_DESC));
	dev->ibdev.owner = THIS_MODULE;
	dev->ibdev.uverbs_abi_ver = OCRDMA_ABI_VERSION;
	dev->ibdev.uverbs_cmd_mask =
	    OCRDMA_UVERBS(GET_CONTEXT) |
	    OCRDMA_UVERBS(QUERY_DEVICE) |
	    OCRDMA_UVERBS(QUERY_PORT) |
	    OCRDMA_UVERBS(ALLOC_PD) |
	    OCRDMA_UVERBS(DEALLOC_PD) |
	    OCRDMA_UVERBS(REG_MR) |
	    OCRDMA_UVERBS(DEREG_MR) |
	    OCRDMA_UVERBS(CREATE_COMP_CHANNEL) |
	    OCRDMA_UVERBS(CREATE_CQ) |
	    OCRDMA_UVERBS(RESIZE_CQ) |
	    OCRDMA_UVERBS(DESTROY_CQ) |
	    OCRDMA_UVERBS(REQ_NOTIFY_CQ) |
	    OCRDMA_UVERBS(CREATE_QP) |
	    OCRDMA_UVERBS(MODIFY_QP) |
	    OCRDMA_UVERBS(QUERY_QP) |
	    OCRDMA_UVERBS(DESTROY_QP) |
	    OCRDMA_UVERBS(POLL_CQ) |
	    OCRDMA_UVERBS(POST_SEND) |
	    OCRDMA_UVERBS(POST_RECV);

	dev->ibdev.uverbs_cmd_mask |=
	    OCRDMA_UVERBS(CREATE_AH) |
	     OCRDMA_UVERBS(MODIFY_AH) |
	     OCRDMA_UVERBS(QUERY_AH) |
	     OCRDMA_UVERBS(DESTROY_AH);

	dev->ibdev.node_type = RDMA_NODE_IB_CA;
	dev->ibdev.phys_port_cnt = 1;
	dev->ibdev.num_comp_vectors = 1;

	/* mandatory verbs. */
	dev->ibdev.query_device = ocrdma_query_device;
	dev->ibdev.query_port = ocrdma_query_port;
	dev->ibdev.modify_port = ocrdma_modify_port;
	dev->ibdev.query_gid = ocrdma_query_gid;
	dev->ibdev.get_link_layer = ocrdma_link_layer;
	dev->ibdev.alloc_pd = ocrdma_alloc_pd;
	dev->ibdev.dealloc_pd = ocrdma_dealloc_pd;

	dev->ibdev.create_cq = ocrdma_create_cq;
	dev->ibdev.destroy_cq = ocrdma_destroy_cq;
	dev->ibdev.resize_cq = ocrdma_resize_cq;

	dev->ibdev.create_qp = ocrdma_create_qp;
	dev->ibdev.modify_qp = ocrdma_modify_qp;
	dev->ibdev.query_qp = ocrdma_query_qp;
	dev->ibdev.destroy_qp = ocrdma_destroy_qp;

	dev->ibdev.query_pkey = ocrdma_query_pkey;
	dev->ibdev.create_ah = ocrdma_create_ah;
	dev->ibdev.destroy_ah = ocrdma_destroy_ah;
	dev->ibdev.query_ah = ocrdma_query_ah;
	dev->ibdev.modify_ah = ocrdma_modify_ah;

	dev->ibdev.poll_cq = ocrdma_poll_cq;
	dev->ibdev.post_send = ocrdma_post_send;
	dev->ibdev.post_recv = ocrdma_post_recv;
	dev->ibdev.req_notify_cq = ocrdma_arm_cq;

	dev->ibdev.get_dma_mr = ocrdma_get_dma_mr;
	dev->ibdev.reg_phys_mr = ocrdma_reg_kernel_mr;
	dev->ibdev.dereg_mr = ocrdma_dereg_mr;
	dev->ibdev.reg_user_mr = ocrdma_reg_user_mr;

	dev->ibdev.alloc_fast_reg_mr = ocrdma_alloc_frmr;
	dev->ibdev.alloc_fast_reg_page_list = ocrdma_alloc_frmr_page_list;
	dev->ibdev.free_fast_reg_page_list = ocrdma_free_frmr_page_list;

	/* mandatory to support user space verbs consumer. */
	dev->ibdev.alloc_ucontext = ocrdma_alloc_ucontext;
	dev->ibdev.dealloc_ucontext = ocrdma_dealloc_ucontext;
	dev->ibdev.mmap = ocrdma_mmap;
	dev->ibdev.dma_device = &dev->nic_info.pdev->dev;

	dev->ibdev.process_mad = ocrdma_process_mad;

	if (ocrdma_get_asic_type(dev) == OCRDMA_ASIC_GEN_SKH_R) {
		dev->ibdev.uverbs_cmd_mask |=
		     OCRDMA_UVERBS(CREATE_SRQ) |
		     OCRDMA_UVERBS(MODIFY_SRQ) |
		     OCRDMA_UVERBS(QUERY_SRQ) |
		     OCRDMA_UVERBS(DESTROY_SRQ) |
		     OCRDMA_UVERBS(POST_SRQ_RECV);

		dev->ibdev.create_srq = ocrdma_create_srq;
		dev->ibdev.modify_srq = ocrdma_modify_srq;
		dev->ibdev.query_srq = ocrdma_query_srq;
		dev->ibdev.destroy_srq = ocrdma_destroy_srq;
		dev->ibdev.post_srq_recv = ocrdma_post_srq_recv;
	}
	return ib_register_device(&dev->ibdev, NULL);
}

static int ocrdma_alloc_resources(struct ocrdma_dev *dev)
{
	mutex_init(&dev->dev_lock);
	dev->sgid_tbl = kzalloc(sizeof(union ib_gid) *
				OCRDMA_MAX_SGID, GFP_KERNEL);
	if (!dev->sgid_tbl)
		goto alloc_err;
	spin_lock_init(&dev->sgid_lock);

	dev->cq_tbl = kzalloc(sizeof(struct ocrdma_cq *) *
			      OCRDMA_MAX_CQ, GFP_KERNEL);
	if (!dev->cq_tbl)
		goto alloc_err;

	if (dev->attr.max_qp) {
		dev->qp_tbl = kzalloc(sizeof(struct ocrdma_qp *) *
				      OCRDMA_MAX_QP, GFP_KERNEL);
		if (!dev->qp_tbl)
			goto alloc_err;
	}
	spin_lock_init(&dev->av_tbl.lock);
	spin_lock_init(&dev->flush_q_lock);
	return 0;
alloc_err:
	pr_err("%s(%d) error.\n", __func__, dev->id);
	return -ENOMEM;
}

static void ocrdma_free_resources(struct ocrdma_dev *dev)
{
	kfree(dev->qp_tbl);
	kfree(dev->cq_tbl);
	kfree(dev->sgid_tbl);
}

/* OCRDMA sysfs interface */
static ssize_t show_rev(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct ocrdma_dev *dev = dev_get_drvdata(device);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", dev->nic_info.pdev->vendor);
}

static ssize_t show_fw_ver(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct ocrdma_dev *dev = dev_get_drvdata(device);

	return scnprintf(buf, PAGE_SIZE, "%s\n", &dev->attr.fw_ver[0]);
}

static ssize_t show_hca_type(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct ocrdma_dev *dev = dev_get_drvdata(device);

	return scnprintf(buf, PAGE_SIZE, "%s\n", &dev->model_number[0]);
}

static DEVICE_ATTR(hw_rev, S_IRUGO, show_rev, NULL);
static DEVICE_ATTR(fw_ver, S_IRUGO, show_fw_ver, NULL);
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca_type, NULL);

static struct device_attribute *ocrdma_attributes[] = {
	&dev_attr_hw_rev,
	&dev_attr_fw_ver,
	&dev_attr_hca_type
};

static void ocrdma_remove_sysfiles(struct ocrdma_dev *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ocrdma_attributes); i++)
		device_remove_file(&dev->ibdev.dev, ocrdma_attributes[i]);
}

static struct ocrdma_dev *ocrdma_add(struct be_dev_info *dev_info)
{
	int status = 0, i;
	struct ocrdma_dev *dev;

	dev = (struct ocrdma_dev *)ib_alloc_device(sizeof(struct ocrdma_dev));
	if (!dev) {
		pr_err("Unable to allocate ib device\n");
		return NULL;
	}
	dev->mbx_cmd = kzalloc(sizeof(struct ocrdma_mqe_emb_cmd), GFP_KERNEL);
	if (!dev->mbx_cmd)
		goto idr_err;

	memcpy(&dev->nic_info, dev_info, sizeof(*dev_info));
	dev->id = idr_alloc(&ocrdma_dev_id, NULL, 0, 0, GFP_KERNEL);
	if (dev->id < 0)
		goto idr_err;

	status = ocrdma_init_hw(dev);
	if (status)
		goto init_err;

	status = ocrdma_alloc_resources(dev);
	if (status)
		goto alloc_err;

	ocrdma_init_service_level(dev);
	status = ocrdma_register_device(dev);
	if (status)
		goto alloc_err;

	for (i = 0; i < ARRAY_SIZE(ocrdma_attributes); i++)
		if (device_create_file(&dev->ibdev.dev, ocrdma_attributes[i]))
			goto sysfs_err;
	spin_lock(&ocrdma_devlist_lock);
	list_add_tail_rcu(&dev->entry, &ocrdma_dev_list);
	spin_unlock(&ocrdma_devlist_lock);
	/* Init stats */
	ocrdma_add_port_stats(dev);

	pr_info("%s %s: %s \"%s\" port %d\n",
		dev_name(&dev->nic_info.pdev->dev), hca_name(dev),
		port_speed_string(dev), dev->model_number,
		dev->hba_port_num);
	pr_info("%s ocrdma%d driver loaded successfully\n",
		dev_name(&dev->nic_info.pdev->dev), dev->id);
	return dev;

sysfs_err:
	ocrdma_remove_sysfiles(dev);
alloc_err:
	ocrdma_free_resources(dev);
	ocrdma_cleanup_hw(dev);
init_err:
	idr_remove(&ocrdma_dev_id, dev->id);
idr_err:
	kfree(dev->mbx_cmd);
	ib_dealloc_device(&dev->ibdev);
	pr_err("%s() leaving. ret=%d\n", __func__, status);
	return NULL;
}

static void ocrdma_remove_free(struct rcu_head *rcu)
{
	struct ocrdma_dev *dev = container_of(rcu, struct ocrdma_dev, rcu);

	idr_remove(&ocrdma_dev_id, dev->id);
	kfree(dev->mbx_cmd);
	ib_dealloc_device(&dev->ibdev);
}

static void ocrdma_remove(struct ocrdma_dev *dev)
{
	/* first unregister with stack to stop all the active traffic
	 * of the registered clients.
	 */
	ocrdma_rem_port_stats(dev);
	ocrdma_remove_sysfiles(dev);

	ib_unregister_device(&dev->ibdev);

	spin_lock(&ocrdma_devlist_lock);
	list_del_rcu(&dev->entry);
	spin_unlock(&ocrdma_devlist_lock);

	ocrdma_free_resources(dev);
	ocrdma_cleanup_hw(dev);

	call_rcu(&dev->rcu, ocrdma_remove_free);
}

static int ocrdma_open(struct ocrdma_dev *dev)
{
	struct ib_event port_event;

	port_event.event = IB_EVENT_PORT_ACTIVE;
	port_event.element.port_num = 1;
	port_event.device = &dev->ibdev;
	ib_dispatch_event(&port_event);
	return 0;
}

static int ocrdma_close(struct ocrdma_dev *dev)
{
	int i;
	struct ocrdma_qp *qp, **cur_qp;
	struct ib_event err_event;
	struct ib_qp_attr attrs;
	int attr_mask = IB_QP_STATE;

	attrs.qp_state = IB_QPS_ERR;
	mutex_lock(&dev->dev_lock);
	if (dev->qp_tbl) {
		cur_qp = dev->qp_tbl;
		for (i = 0; i < OCRDMA_MAX_QP; i++) {
			qp = cur_qp[i];
			if (qp && qp->ibqp.qp_type != IB_QPT_GSI) {
				/* change the QP state to ERROR */
				_ocrdma_modify_qp(&qp->ibqp, &attrs, attr_mask);

				err_event.event = IB_EVENT_QP_FATAL;
				err_event.element.qp = &qp->ibqp;
				err_event.device = &dev->ibdev;
				ib_dispatch_event(&err_event);
			}
		}
	}
	mutex_unlock(&dev->dev_lock);

	err_event.event = IB_EVENT_PORT_ERR;
	err_event.element.port_num = 1;
	err_event.device = &dev->ibdev;
	ib_dispatch_event(&err_event);
	return 0;
}

/* event handling via NIC driver ensures that all the NIC specific
 * initialization done before RoCE driver notifies
 * event to stack.
 */
static void ocrdma_event_handler(struct ocrdma_dev *dev, u32 event)
{
	switch (event) {
	case BE_DEV_UP:
		ocrdma_open(dev);
		break;
	case BE_DEV_DOWN:
		ocrdma_close(dev);
		break;
	}
}

static struct ocrdma_driver ocrdma_drv = {
	.name			= "ocrdma_driver",
	.add			= ocrdma_add,
	.remove			= ocrdma_remove,
	.state_change_handler	= ocrdma_event_handler,
	.be_abi_version		= OCRDMA_BE_ROCE_ABI_VERSION,
};

static void ocrdma_unregister_inet6addr_notifier(void)
{
#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&ocrdma_inet6addr_notifier);
#endif
}

static void ocrdma_unregister_inetaddr_notifier(void)
{
	unregister_inetaddr_notifier(&ocrdma_inetaddr_notifier);
}

static int __init ocrdma_init_module(void)
{
	int status;

	ocrdma_init_debugfs();

	status = register_inetaddr_notifier(&ocrdma_inetaddr_notifier);
	if (status)
		return status;

#if IS_ENABLED(CONFIG_IPV6)
	status = register_inet6addr_notifier(&ocrdma_inet6addr_notifier);
	if (status)
		goto err_notifier6;
#endif

	status = be_roce_register_driver(&ocrdma_drv);
	if (status)
		goto err_be_reg;

	return 0;

err_be_reg:
	ocrdma_unregister_inet6addr_notifier();
err_notifier6:
	ocrdma_unregister_inetaddr_notifier();
	return status;
}

static void __exit ocrdma_exit_module(void)
{
	be_roce_unregister_driver(&ocrdma_drv);
	ocrdma_unregister_inet6addr_notifier();
	ocrdma_unregister_inetaddr_notifier();
	ocrdma_rem_debugfs();
}

module_init(ocrdma_init_module);
module_exit(ocrdma_exit_module);
