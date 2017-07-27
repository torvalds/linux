/* This file is part of the Emulex RoCE Device Driver for
 * RoCE (RDMA over Converged Ethernet) adapters.
 * Copyright (C) 2012-2015 Emulex. All rights reserved.
 * EMULEX and SLI are trademarks of Emulex.
 * www.emulex.com
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include <linux/module.h>
#include <linux/idr.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_mad.h>

#include <linux/netdevice.h>
#include <net/addrconf.h>

#include "ocrdma.h"
#include "ocrdma_verbs.h"
#include "ocrdma_ah.h"
#include "be_roce.h"
#include "ocrdma_hw.h"
#include "ocrdma_stats.h"
#include <rdma/ocrdma-abi.h>

MODULE_DESCRIPTION(OCRDMA_ROCE_DRV_DESC " " OCRDMA_ROCE_DRV_VERSION);
MODULE_AUTHOR("Emulex Corporation");
MODULE_LICENSE("Dual BSD/GPL");

static DEFINE_IDR(ocrdma_dev_id);

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
static enum rdma_link_layer ocrdma_link_layer(struct ib_device *device,
					      u8 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

static int ocrdma_port_immutable(struct ib_device *ibdev, u8 port_num,
			         struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	struct ocrdma_dev *dev;
	int err;

	dev = get_ocrdma_dev(ibdev);
	immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE;
	if (ocrdma_is_udp_encap_supported(dev))
		immutable->core_cap_flags |= RDMA_CORE_CAP_PROT_ROCE_UDP_ENCAP;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	immutable->max_mad_size = IB_MGMT_MAD_SIZE;

	return 0;
}

static void get_dev_fw_str(struct ib_device *device, char *str,
			   size_t str_len)
{
	struct ocrdma_dev *dev = get_ocrdma_dev(device);

	snprintf(str, str_len, "%s", &dev->attr.fw_ver[0]);
}

static int ocrdma_register_device(struct ocrdma_dev *dev)
{
	strlcpy(dev->ibdev.name, "ocrdma%d", IB_DEVICE_NAME_MAX);
	ocrdma_get_guid(dev, (u8 *)&dev->ibdev.node_guid);
	BUILD_BUG_ON(sizeof(OCRDMA_NODE_DESC) > IB_DEVICE_NODE_DESC_MAX);
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
	dev->ibdev.num_comp_vectors = dev->eq_cnt;

	/* mandatory verbs. */
	dev->ibdev.query_device = ocrdma_query_device;
	dev->ibdev.query_port = ocrdma_query_port;
	dev->ibdev.modify_port = ocrdma_modify_port;
	dev->ibdev.query_gid = ocrdma_query_gid;
	dev->ibdev.get_netdev = ocrdma_get_netdev;
	dev->ibdev.add_gid = ocrdma_add_gid;
	dev->ibdev.del_gid = ocrdma_del_gid;
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
	dev->ibdev.dereg_mr = ocrdma_dereg_mr;
	dev->ibdev.reg_user_mr = ocrdma_reg_user_mr;

	dev->ibdev.alloc_mr = ocrdma_alloc_mr;
	dev->ibdev.map_mr_sg = ocrdma_map_mr_sg;

	/* mandatory to support user space verbs consumer. */
	dev->ibdev.alloc_ucontext = ocrdma_alloc_ucontext;
	dev->ibdev.dealloc_ucontext = ocrdma_dealloc_ucontext;
	dev->ibdev.mmap = ocrdma_mmap;
	dev->ibdev.dev.parent = &dev->nic_info.pdev->dev;

	dev->ibdev.process_mad = ocrdma_process_mad;
	dev->ibdev.get_port_immutable = ocrdma_port_immutable;
	dev->ibdev.get_dev_fw_str     = get_dev_fw_str;

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

	dev->stag_arr = kzalloc(sizeof(u64) * OCRDMA_MAX_STAG, GFP_KERNEL);
	if (dev->stag_arr == NULL)
		goto alloc_err;

	ocrdma_alloc_pd_pool(dev);

	if (!ocrdma_alloc_stats_resources(dev)) {
		pr_err("%s: stats resource allocation failed\n", __func__);
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
	ocrdma_release_stats_resources(dev);
	kfree(dev->stag_arr);
	kfree(dev->qp_tbl);
	kfree(dev->cq_tbl);
}

/* OCRDMA sysfs interface */
static ssize_t show_rev(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct ocrdma_dev *dev = dev_get_drvdata(device);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", dev->nic_info.pdev->vendor);
}

static ssize_t show_hca_type(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct ocrdma_dev *dev = dev_get_drvdata(device);

	return scnprintf(buf, PAGE_SIZE, "%s\n", &dev->model_number[0]);
}

static DEVICE_ATTR(hw_rev, S_IRUGO, show_rev, NULL);
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca_type, NULL);

static struct device_attribute *ocrdma_attributes[] = {
	&dev_attr_hw_rev,
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
	u8 lstate = 0;
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

	/* Query Link state and update */
	status = ocrdma_mbx_get_link_speed(dev, NULL, &lstate);
	if (!status)
		ocrdma_update_link_state(dev, lstate);

	for (i = 0; i < ARRAY_SIZE(ocrdma_attributes); i++)
		if (device_create_file(&dev->ibdev.dev, ocrdma_attributes[i]))
			goto sysfs_err;
	/* Init stats */
	ocrdma_add_port_stats(dev);
	/* Interrupt Moderation */
	INIT_DELAYED_WORK(&dev->eqd_work, ocrdma_eqd_set_task);
	schedule_delayed_work(&dev->eqd_work, msecs_to_jiffies(1000));

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

static void ocrdma_remove_free(struct ocrdma_dev *dev)
{

	idr_remove(&ocrdma_dev_id, dev->id);
	kfree(dev->mbx_cmd);
	ib_dealloc_device(&dev->ibdev);
}

static void ocrdma_remove(struct ocrdma_dev *dev)
{
	/* first unregister with stack to stop all the active traffic
	 * of the registered clients.
	 */
	cancel_delayed_work_sync(&dev->eqd_work);
	ocrdma_remove_sysfiles(dev);
	ib_unregister_device(&dev->ibdev);

	ocrdma_rem_port_stats(dev);
	ocrdma_free_resources(dev);
	ocrdma_cleanup_hw(dev);
	ocrdma_remove_free(dev);
}

static int ocrdma_dispatch_port_active(struct ocrdma_dev *dev)
{
	struct ib_event port_event;

	port_event.event = IB_EVENT_PORT_ACTIVE;
	port_event.element.port_num = 1;
	port_event.device = &dev->ibdev;
	ib_dispatch_event(&port_event);
	return 0;
}

static int ocrdma_dispatch_port_error(struct ocrdma_dev *dev)
{
	struct ib_event err_event;

	err_event.event = IB_EVENT_PORT_ERR;
	err_event.element.port_num = 1;
	err_event.device = &dev->ibdev;
	ib_dispatch_event(&err_event);
	return 0;
}

static void ocrdma_shutdown(struct ocrdma_dev *dev)
{
	ocrdma_dispatch_port_error(dev);
	ocrdma_remove(dev);
}

/* event handling via NIC driver ensures that all the NIC specific
 * initialization done before RoCE driver notifies
 * event to stack.
 */
static void ocrdma_event_handler(struct ocrdma_dev *dev, u32 event)
{
	switch (event) {
	case BE_DEV_SHUTDOWN:
		ocrdma_shutdown(dev);
		break;
	default:
		break;
	}
}

void ocrdma_update_link_state(struct ocrdma_dev *dev, u8 lstate)
{
	if (!(dev->flags & OCRDMA_FLAGS_LINK_STATUS_INIT)) {
		dev->flags |= OCRDMA_FLAGS_LINK_STATUS_INIT;
		if (!lstate)
			return;
	}

	if (!lstate)
		ocrdma_dispatch_port_error(dev);
	else
		ocrdma_dispatch_port_active(dev);
}

static struct ocrdma_driver ocrdma_drv = {
	.name			= "ocrdma_driver",
	.add			= ocrdma_add,
	.remove			= ocrdma_remove,
	.state_change_handler	= ocrdma_event_handler,
	.be_abi_version		= OCRDMA_BE_ROCE_ABI_VERSION,
};

static int __init ocrdma_init_module(void)
{
	int status;

	ocrdma_init_debugfs();

	status = be_roce_register_driver(&ocrdma_drv);
	if (status)
		goto err_be_reg;

	return 0;

err_be_reg:

	return status;
}

static void __exit ocrdma_exit_module(void)
{
	be_roce_unregister_driver(&ocrdma_drv);
	ocrdma_rem_debugfs();
	idr_destroy(&ocrdma_dev_id);
}

module_init(ocrdma_init_module);
module_exit(ocrdma_exit_module);
