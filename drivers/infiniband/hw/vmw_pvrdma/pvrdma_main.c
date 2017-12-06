/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/errno.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_user_verbs.h>
#include <net/addrconf.h>

#include "pvrdma.h"

#define DRV_NAME	"vmw_pvrdma"
#define DRV_VERSION	"1.0.1.0-k"

static DEFINE_MUTEX(pvrdma_device_list_lock);
static LIST_HEAD(pvrdma_device_list);
static struct workqueue_struct *event_wq;

static int pvrdma_add_gid(struct ib_device *ibdev,
			  u8 port_num,
			  unsigned int index,
			  const union ib_gid *gid,
			  const struct ib_gid_attr *attr,
			  void **context);
static int pvrdma_del_gid(struct ib_device *ibdev,
			  u8 port_num,
			  unsigned int index,
			  void **context);


static ssize_t show_hca(struct device *device, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "VMW_PVRDMA-%s\n", DRV_VERSION);
}

static ssize_t show_rev(struct device *device, struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", PVRDMA_REV_ID);
}

static ssize_t show_board(struct device *device, struct device_attribute *attr,
			  char *buf)
{
	return sprintf(buf, "%d\n", PVRDMA_BOARD_ID);
}

static DEVICE_ATTR(hw_rev,   S_IRUGO, show_rev,	   NULL);
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca,	   NULL);
static DEVICE_ATTR(board_id, S_IRUGO, show_board,  NULL);

static struct device_attribute *pvrdma_class_attributes[] = {
	&dev_attr_hw_rev,
	&dev_attr_hca_type,
	&dev_attr_board_id
};

static void pvrdma_get_fw_ver_str(struct ib_device *device, char *str)
{
	struct pvrdma_dev *dev =
		container_of(device, struct pvrdma_dev, ib_dev);
	snprintf(str, IB_FW_VERSION_NAME_MAX, "%d.%d.%d\n",
		 (int) (dev->dsr->caps.fw_ver >> 32),
		 (int) (dev->dsr->caps.fw_ver >> 16) & 0xffff,
		 (int) dev->dsr->caps.fw_ver & 0xffff);
}

static int pvrdma_init_device(struct pvrdma_dev *dev)
{
	/*  Initialize some device related stuff */
	spin_lock_init(&dev->cmd_lock);
	sema_init(&dev->cmd_sema, 1);
	atomic_set(&dev->num_qps, 0);
	atomic_set(&dev->num_cqs, 0);
	atomic_set(&dev->num_pds, 0);
	atomic_set(&dev->num_ahs, 0);

	return 0;
}

static int pvrdma_port_immutable(struct ib_device *ibdev, u8 port_num,
				 struct ib_port_immutable *immutable)
{
	struct pvrdma_dev *dev = to_vdev(ibdev);
	struct ib_port_attr attr;
	int err;

	if (dev->dsr->caps.gid_types == PVRDMA_GID_TYPE_FLAG_ROCE_V1)
		immutable->core_cap_flags |= RDMA_CORE_PORT_IBA_ROCE;
	else if (dev->dsr->caps.gid_types == PVRDMA_GID_TYPE_FLAG_ROCE_V2)
		immutable->core_cap_flags |= RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	immutable->max_mad_size = IB_MGMT_MAD_SIZE;
	return 0;
}

static struct net_device *pvrdma_get_netdev(struct ib_device *ibdev,
					    u8 port_num)
{
	struct net_device *netdev;
	struct pvrdma_dev *dev = to_vdev(ibdev);

	if (port_num != 1)
		return NULL;

	rcu_read_lock();
	netdev = dev->netdev;
	if (netdev)
		dev_hold(netdev);
	rcu_read_unlock();

	return netdev;
}

static int pvrdma_register_device(struct pvrdma_dev *dev)
{
	int ret = -1;
	int i = 0;

	strlcpy(dev->ib_dev.name, "vmw_pvrdma%d", IB_DEVICE_NAME_MAX);
	dev->ib_dev.node_guid = dev->dsr->caps.node_guid;
	dev->sys_image_guid = dev->dsr->caps.sys_image_guid;
	dev->flags = 0;
	dev->ib_dev.owner = THIS_MODULE;
	dev->ib_dev.num_comp_vectors = 1;
	dev->ib_dev.dev.parent = &dev->pdev->dev;
	dev->ib_dev.uverbs_abi_ver = PVRDMA_UVERBS_ABI_VERSION;
	dev->ib_dev.uverbs_cmd_mask =
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE)	|
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT)		|
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_REG_MR)		|
		(1ull << IB_USER_VERBS_CMD_DEREG_MR)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL)	|
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ)		|
		(1ull << IB_USER_VERBS_CMD_POLL_CQ)		|
		(1ull << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ)	|
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_QP)		|
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_QP)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP)		|
		(1ull << IB_USER_VERBS_CMD_POST_SEND)		|
		(1ull << IB_USER_VERBS_CMD_POST_RECV)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_AH)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_AH);

	dev->ib_dev.node_type = RDMA_NODE_IB_CA;
	dev->ib_dev.phys_port_cnt = dev->dsr->caps.phys_port_cnt;

	dev->ib_dev.query_device = pvrdma_query_device;
	dev->ib_dev.query_port = pvrdma_query_port;
	dev->ib_dev.query_gid = pvrdma_query_gid;
	dev->ib_dev.query_pkey = pvrdma_query_pkey;
	dev->ib_dev.modify_port	= pvrdma_modify_port;
	dev->ib_dev.alloc_ucontext = pvrdma_alloc_ucontext;
	dev->ib_dev.dealloc_ucontext = pvrdma_dealloc_ucontext;
	dev->ib_dev.mmap = pvrdma_mmap;
	dev->ib_dev.alloc_pd = pvrdma_alloc_pd;
	dev->ib_dev.dealloc_pd = pvrdma_dealloc_pd;
	dev->ib_dev.create_ah = pvrdma_create_ah;
	dev->ib_dev.destroy_ah = pvrdma_destroy_ah;
	dev->ib_dev.create_qp = pvrdma_create_qp;
	dev->ib_dev.modify_qp = pvrdma_modify_qp;
	dev->ib_dev.query_qp = pvrdma_query_qp;
	dev->ib_dev.destroy_qp = pvrdma_destroy_qp;
	dev->ib_dev.post_send = pvrdma_post_send;
	dev->ib_dev.post_recv = pvrdma_post_recv;
	dev->ib_dev.create_cq = pvrdma_create_cq;
	dev->ib_dev.modify_cq = pvrdma_modify_cq;
	dev->ib_dev.resize_cq = pvrdma_resize_cq;
	dev->ib_dev.destroy_cq = pvrdma_destroy_cq;
	dev->ib_dev.poll_cq = pvrdma_poll_cq;
	dev->ib_dev.req_notify_cq = pvrdma_req_notify_cq;
	dev->ib_dev.get_dma_mr = pvrdma_get_dma_mr;
	dev->ib_dev.reg_user_mr	= pvrdma_reg_user_mr;
	dev->ib_dev.dereg_mr = pvrdma_dereg_mr;
	dev->ib_dev.alloc_mr = pvrdma_alloc_mr;
	dev->ib_dev.map_mr_sg = pvrdma_map_mr_sg;
	dev->ib_dev.add_gid = pvrdma_add_gid;
	dev->ib_dev.del_gid = pvrdma_del_gid;
	dev->ib_dev.get_netdev = pvrdma_get_netdev;
	dev->ib_dev.get_port_immutable = pvrdma_port_immutable;
	dev->ib_dev.get_link_layer = pvrdma_port_link_layer;
	dev->ib_dev.get_dev_fw_str = pvrdma_get_fw_ver_str;

	mutex_init(&dev->port_mutex);
	spin_lock_init(&dev->desc_lock);

	dev->cq_tbl = kcalloc(dev->dsr->caps.max_cq, sizeof(void *),
			      GFP_KERNEL);
	if (!dev->cq_tbl)
		return ret;
	spin_lock_init(&dev->cq_tbl_lock);

	dev->qp_tbl = kcalloc(dev->dsr->caps.max_qp, sizeof(void *),
			      GFP_KERNEL);
	if (!dev->qp_tbl)
		goto err_cq_free;
	spin_lock_init(&dev->qp_tbl_lock);

	ret = ib_register_device(&dev->ib_dev, NULL);
	if (ret)
		goto err_qp_free;

	for (i = 0; i < ARRAY_SIZE(pvrdma_class_attributes); ++i) {
		ret = device_create_file(&dev->ib_dev.dev,
					 pvrdma_class_attributes[i]);
		if (ret)
			goto err_class;
	}

	dev->ib_active = true;

	return 0;

err_class:
	ib_unregister_device(&dev->ib_dev);
err_qp_free:
	kfree(dev->qp_tbl);
err_cq_free:
	kfree(dev->cq_tbl);

	return ret;
}

static irqreturn_t pvrdma_intr0_handler(int irq, void *dev_id)
{
	u32 icr = PVRDMA_INTR_CAUSE_RESPONSE;
	struct pvrdma_dev *dev = dev_id;

	dev_dbg(&dev->pdev->dev, "interrupt 0 (response) handler\n");

	if (!dev->pdev->msix_enabled) {
		/* Legacy intr */
		icr = pvrdma_read_reg(dev, PVRDMA_REG_ICR);
		if (icr == 0)
			return IRQ_NONE;
	}

	if (icr == PVRDMA_INTR_CAUSE_RESPONSE)
		complete(&dev->cmd_done);

	return IRQ_HANDLED;
}

static void pvrdma_qp_event(struct pvrdma_dev *dev, u32 qpn, int type)
{
	struct pvrdma_qp *qp;
	unsigned long flags;

	spin_lock_irqsave(&dev->qp_tbl_lock, flags);
	qp = dev->qp_tbl[qpn % dev->dsr->caps.max_qp];
	if (qp)
		atomic_inc(&qp->refcnt);
	spin_unlock_irqrestore(&dev->qp_tbl_lock, flags);

	if (qp && qp->ibqp.event_handler) {
		struct ib_qp *ibqp = &qp->ibqp;
		struct ib_event e;

		e.device = ibqp->device;
		e.element.qp = ibqp;
		e.event = type; /* 1:1 mapping for now. */
		ibqp->event_handler(&e, ibqp->qp_context);
	}
	if (qp) {
		atomic_dec(&qp->refcnt);
		if (atomic_read(&qp->refcnt) == 0)
			wake_up(&qp->wait);
	}
}

static void pvrdma_cq_event(struct pvrdma_dev *dev, u32 cqn, int type)
{
	struct pvrdma_cq *cq;
	unsigned long flags;

	spin_lock_irqsave(&dev->cq_tbl_lock, flags);
	cq = dev->cq_tbl[cqn % dev->dsr->caps.max_cq];
	if (cq)
		atomic_inc(&cq->refcnt);
	spin_unlock_irqrestore(&dev->cq_tbl_lock, flags);

	if (cq && cq->ibcq.event_handler) {
		struct ib_cq *ibcq = &cq->ibcq;
		struct ib_event e;

		e.device = ibcq->device;
		e.element.cq = ibcq;
		e.event = type; /* 1:1 mapping for now. */
		ibcq->event_handler(&e, ibcq->cq_context);
	}
	if (cq) {
		atomic_dec(&cq->refcnt);
		if (atomic_read(&cq->refcnt) == 0)
			wake_up(&cq->wait);
	}
}

static void pvrdma_dispatch_event(struct pvrdma_dev *dev, int port,
				  enum ib_event_type event)
{
	struct ib_event ib_event;

	memset(&ib_event, 0, sizeof(ib_event));
	ib_event.device = &dev->ib_dev;
	ib_event.element.port_num = port;
	ib_event.event = event;
	ib_dispatch_event(&ib_event);
}

static void pvrdma_dev_event(struct pvrdma_dev *dev, u8 port, int type)
{
	if (port < 1 || port > dev->dsr->caps.phys_port_cnt) {
		dev_warn(&dev->pdev->dev, "event on port %d\n", port);
		return;
	}

	pvrdma_dispatch_event(dev, port, type);
}

static inline struct pvrdma_eqe *get_eqe(struct pvrdma_dev *dev, unsigned int i)
{
	return (struct pvrdma_eqe *)pvrdma_page_dir_get_ptr(
					&dev->async_pdir,
					PAGE_SIZE +
					sizeof(struct pvrdma_eqe) * i);
}

static irqreturn_t pvrdma_intr1_handler(int irq, void *dev_id)
{
	struct pvrdma_dev *dev = dev_id;
	struct pvrdma_ring *ring = &dev->async_ring_state->rx;
	int ring_slots = (dev->dsr->async_ring_pages.num_pages - 1) *
			 PAGE_SIZE / sizeof(struct pvrdma_eqe);
	unsigned int head;

	dev_dbg(&dev->pdev->dev, "interrupt 1 (async event) handler\n");

	/*
	 * Don't process events until the IB device is registered. Otherwise
	 * we'll try to ib_dispatch_event() on an invalid device.
	 */
	if (!dev->ib_active)
		return IRQ_HANDLED;

	while (pvrdma_idx_ring_has_data(ring, ring_slots, &head) > 0) {
		struct pvrdma_eqe *eqe;

		eqe = get_eqe(dev, head);

		switch (eqe->type) {
		case PVRDMA_EVENT_QP_FATAL:
		case PVRDMA_EVENT_QP_REQ_ERR:
		case PVRDMA_EVENT_QP_ACCESS_ERR:
		case PVRDMA_EVENT_COMM_EST:
		case PVRDMA_EVENT_SQ_DRAINED:
		case PVRDMA_EVENT_PATH_MIG:
		case PVRDMA_EVENT_PATH_MIG_ERR:
		case PVRDMA_EVENT_QP_LAST_WQE_REACHED:
			pvrdma_qp_event(dev, eqe->info, eqe->type);
			break;

		case PVRDMA_EVENT_CQ_ERR:
			pvrdma_cq_event(dev, eqe->info, eqe->type);
			break;

		case PVRDMA_EVENT_SRQ_ERR:
		case PVRDMA_EVENT_SRQ_LIMIT_REACHED:
			break;

		case PVRDMA_EVENT_PORT_ACTIVE:
		case PVRDMA_EVENT_PORT_ERR:
		case PVRDMA_EVENT_LID_CHANGE:
		case PVRDMA_EVENT_PKEY_CHANGE:
		case PVRDMA_EVENT_SM_CHANGE:
		case PVRDMA_EVENT_CLIENT_REREGISTER:
		case PVRDMA_EVENT_GID_CHANGE:
			pvrdma_dev_event(dev, eqe->info, eqe->type);
			break;

		case PVRDMA_EVENT_DEVICE_FATAL:
			pvrdma_dev_event(dev, 1, eqe->type);
			break;

		default:
			break;
		}

		pvrdma_idx_ring_inc(&ring->cons_head, ring_slots);
	}

	return IRQ_HANDLED;
}

static inline struct pvrdma_cqne *get_cqne(struct pvrdma_dev *dev,
					   unsigned int i)
{
	return (struct pvrdma_cqne *)pvrdma_page_dir_get_ptr(
					&dev->cq_pdir,
					PAGE_SIZE +
					sizeof(struct pvrdma_cqne) * i);
}

static irqreturn_t pvrdma_intrx_handler(int irq, void *dev_id)
{
	struct pvrdma_dev *dev = dev_id;
	struct pvrdma_ring *ring = &dev->cq_ring_state->rx;
	int ring_slots = (dev->dsr->cq_ring_pages.num_pages - 1) * PAGE_SIZE /
			 sizeof(struct pvrdma_cqne);
	unsigned int head;
	unsigned long flags;

	dev_dbg(&dev->pdev->dev, "interrupt x (completion) handler\n");

	while (pvrdma_idx_ring_has_data(ring, ring_slots, &head) > 0) {
		struct pvrdma_cqne *cqne;
		struct pvrdma_cq *cq;

		cqne = get_cqne(dev, head);
		spin_lock_irqsave(&dev->cq_tbl_lock, flags);
		cq = dev->cq_tbl[cqne->info % dev->dsr->caps.max_cq];
		if (cq)
			atomic_inc(&cq->refcnt);
		spin_unlock_irqrestore(&dev->cq_tbl_lock, flags);

		if (cq && cq->ibcq.comp_handler)
			cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
		if (cq) {
			atomic_dec(&cq->refcnt);
			if (atomic_read(&cq->refcnt))
				wake_up(&cq->wait);
		}
		pvrdma_idx_ring_inc(&ring->cons_head, ring_slots);
	}

	return IRQ_HANDLED;
}

static void pvrdma_free_irq(struct pvrdma_dev *dev)
{
	int i;

	dev_dbg(&dev->pdev->dev, "freeing interrupts\n");
	for (i = 0; i < dev->nr_vectors; i++)
		free_irq(pci_irq_vector(dev->pdev, i), dev);
}

static void pvrdma_enable_intrs(struct pvrdma_dev *dev)
{
	dev_dbg(&dev->pdev->dev, "enable interrupts\n");
	pvrdma_write_reg(dev, PVRDMA_REG_IMR, 0);
}

static void pvrdma_disable_intrs(struct pvrdma_dev *dev)
{
	dev_dbg(&dev->pdev->dev, "disable interrupts\n");
	pvrdma_write_reg(dev, PVRDMA_REG_IMR, ~0);
}

static int pvrdma_alloc_intrs(struct pvrdma_dev *dev)
{
	struct pci_dev *pdev = dev->pdev;
	int ret = 0, i;

	ret = pci_alloc_irq_vectors(pdev, 1, PVRDMA_MAX_INTERRUPTS,
			PCI_IRQ_MSIX);
	if (ret < 0) {
		ret = pci_alloc_irq_vectors(pdev, 1, 1,
				PCI_IRQ_MSI | PCI_IRQ_LEGACY);
		if (ret < 0)
			return ret;
	}
	dev->nr_vectors = ret;

	ret = request_irq(pci_irq_vector(dev->pdev, 0), pvrdma_intr0_handler,
			pdev->msix_enabled ? 0 : IRQF_SHARED, DRV_NAME, dev);
	if (ret) {
		dev_err(&dev->pdev->dev,
			"failed to request interrupt 0\n");
		goto out_free_vectors;
	}

	for (i = 1; i < dev->nr_vectors; i++) {
		ret = request_irq(pci_irq_vector(dev->pdev, i),
				i == 1 ? pvrdma_intr1_handler :
					 pvrdma_intrx_handler,
				0, DRV_NAME, dev);
		if (ret) {
			dev_err(&dev->pdev->dev,
				"failed to request interrupt %d\n", i);
			goto free_irqs;
		}
	}

	return 0;

free_irqs:
	while (--i >= 0)
		free_irq(pci_irq_vector(dev->pdev, i), dev);
out_free_vectors:
	pci_free_irq_vectors(pdev);
	return ret;
}

static void pvrdma_free_slots(struct pvrdma_dev *dev)
{
	struct pci_dev *pdev = dev->pdev;

	if (dev->resp_slot)
		dma_free_coherent(&pdev->dev, PAGE_SIZE, dev->resp_slot,
				  dev->dsr->resp_slot_dma);
	if (dev->cmd_slot)
		dma_free_coherent(&pdev->dev, PAGE_SIZE, dev->cmd_slot,
				  dev->dsr->cmd_slot_dma);
}

static int pvrdma_add_gid_at_index(struct pvrdma_dev *dev,
				   const union ib_gid *gid,
				   u8 gid_type,
				   int index)
{
	int ret;
	union pvrdma_cmd_req req;
	struct pvrdma_cmd_create_bind *cmd_bind = &req.create_bind;

	if (!dev->sgid_tbl) {
		dev_warn(&dev->pdev->dev, "sgid table not initialized\n");
		return -EINVAL;
	}

	memset(cmd_bind, 0, sizeof(*cmd_bind));
	cmd_bind->hdr.cmd = PVRDMA_CMD_CREATE_BIND;
	memcpy(cmd_bind->new_gid, gid->raw, 16);
	cmd_bind->mtu = ib_mtu_enum_to_int(IB_MTU_1024);
	cmd_bind->vlan = 0xfff;
	cmd_bind->index = index;
	cmd_bind->gid_type = gid_type;

	ret = pvrdma_cmd_post(dev, &req, NULL, 0);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not create binding, error: %d\n", ret);
		return -EFAULT;
	}
	memcpy(&dev->sgid_tbl[index], gid, sizeof(*gid));
	return 0;
}

static int pvrdma_add_gid(struct ib_device *ibdev,
			  u8 port_num,
			  unsigned int index,
			  const union ib_gid *gid,
			  const struct ib_gid_attr *attr,
			  void **context)
{
	struct pvrdma_dev *dev = to_vdev(ibdev);

	return pvrdma_add_gid_at_index(dev, gid,
				       ib_gid_type_to_pvrdma(attr->gid_type),
				       index);
}

static int pvrdma_del_gid_at_index(struct pvrdma_dev *dev, int index)
{
	int ret;
	union pvrdma_cmd_req req;
	struct pvrdma_cmd_destroy_bind *cmd_dest = &req.destroy_bind;

	/* Update sgid table. */
	if (!dev->sgid_tbl) {
		dev_warn(&dev->pdev->dev, "sgid table not initialized\n");
		return -EINVAL;
	}

	memset(cmd_dest, 0, sizeof(*cmd_dest));
	cmd_dest->hdr.cmd = PVRDMA_CMD_DESTROY_BIND;
	memcpy(cmd_dest->dest_gid, &dev->sgid_tbl[index], 16);
	cmd_dest->index = index;

	ret = pvrdma_cmd_post(dev, &req, NULL, 0);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not destroy binding, error: %d\n", ret);
		return ret;
	}
	memset(&dev->sgid_tbl[index], 0, 16);
	return 0;
}

static int pvrdma_del_gid(struct ib_device *ibdev,
			  u8 port_num,
			  unsigned int index,
			  void **context)
{
	struct pvrdma_dev *dev = to_vdev(ibdev);

	dev_dbg(&dev->pdev->dev, "removing gid at index %u from %s",
		index, dev->netdev->name);

	return pvrdma_del_gid_at_index(dev, index);
}

static void pvrdma_netdevice_event_handle(struct pvrdma_dev *dev,
					  unsigned long event)
{
	switch (event) {
	case NETDEV_REBOOT:
	case NETDEV_DOWN:
		pvrdma_dispatch_event(dev, 1, IB_EVENT_PORT_ERR);
		break;
	case NETDEV_UP:
		pvrdma_write_reg(dev, PVRDMA_REG_CTL,
				 PVRDMA_DEVICE_CTL_UNQUIESCE);

		mb();

		if (pvrdma_read_reg(dev, PVRDMA_REG_ERR))
			dev_err(&dev->pdev->dev,
				"failed to activate device during link up\n");
		else
			pvrdma_dispatch_event(dev, 1, IB_EVENT_PORT_ACTIVE);
		break;
	default:
		dev_dbg(&dev->pdev->dev, "ignore netdevice event %ld on %s\n",
			event, dev->ib_dev.name);
		break;
	}
}

static void pvrdma_netdevice_event_work(struct work_struct *work)
{
	struct pvrdma_netdevice_work *netdev_work;
	struct pvrdma_dev *dev;

	netdev_work = container_of(work, struct pvrdma_netdevice_work, work);

	mutex_lock(&pvrdma_device_list_lock);
	list_for_each_entry(dev, &pvrdma_device_list, device_link) {
		if (dev->netdev == netdev_work->event_netdev) {
			pvrdma_netdevice_event_handle(dev, netdev_work->event);
			break;
		}
	}
	mutex_unlock(&pvrdma_device_list_lock);

	kfree(netdev_work);
}

static int pvrdma_netdevice_event(struct notifier_block *this,
				  unsigned long event, void *ptr)
{
	struct net_device *event_netdev = netdev_notifier_info_to_dev(ptr);
	struct pvrdma_netdevice_work *netdev_work;

	netdev_work = kmalloc(sizeof(*netdev_work), GFP_ATOMIC);
	if (!netdev_work)
		return NOTIFY_BAD;

	INIT_WORK(&netdev_work->work, pvrdma_netdevice_event_work);
	netdev_work->event_netdev = event_netdev;
	netdev_work->event = event;
	queue_work(event_wq, &netdev_work->work);

	return NOTIFY_DONE;
}

static int pvrdma_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct pci_dev *pdev_net;
	struct pvrdma_dev *dev;
	int ret;
	unsigned long start;
	unsigned long len;
	dma_addr_t slot_dma = 0;

	dev_dbg(&pdev->dev, "initializing driver %s\n", pci_name(pdev));

	/* Allocate zero-out device */
	dev = (struct pvrdma_dev *)ib_alloc_device(sizeof(*dev));
	if (!dev) {
		dev_err(&pdev->dev, "failed to allocate IB device\n");
		return -ENOMEM;
	}

	mutex_lock(&pvrdma_device_list_lock);
	list_add(&dev->device_link, &pvrdma_device_list);
	mutex_unlock(&pvrdma_device_list_lock);

	ret = pvrdma_init_device(dev);
	if (ret)
		goto err_free_device;

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "cannot enable PCI device\n");
		goto err_free_device;
	}

	dev_dbg(&pdev->dev, "PCI resource flags BAR0 %#lx\n",
		pci_resource_flags(pdev, 0));
	dev_dbg(&pdev->dev, "PCI resource len %#llx\n",
		(unsigned long long)pci_resource_len(pdev, 0));
	dev_dbg(&pdev->dev, "PCI resource start %#llx\n",
		(unsigned long long)pci_resource_start(pdev, 0));
	dev_dbg(&pdev->dev, "PCI resource flags BAR1 %#lx\n",
		pci_resource_flags(pdev, 1));
	dev_dbg(&pdev->dev, "PCI resource len %#llx\n",
		(unsigned long long)pci_resource_len(pdev, 1));
	dev_dbg(&pdev->dev, "PCI resource start %#llx\n",
		(unsigned long long)pci_resource_start(pdev, 1));

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
	    !(pci_resource_flags(pdev, 1) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "PCI BAR region not MMIO\n");
		ret = -ENOMEM;
		goto err_free_device;
	}

	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "cannot request PCI resources\n");
		goto err_disable_pdev;
	}

	/* Enable 64-Bit DMA */
	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) == 0) {
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (ret != 0) {
			dev_err(&pdev->dev,
				"pci_set_consistent_dma_mask failed\n");
			goto err_free_resource;
		}
	} else {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret != 0) {
			dev_err(&pdev->dev,
				"pci_set_dma_mask failed\n");
			goto err_free_resource;
		}
	}

	pci_set_master(pdev);

	/* Map register space */
	start = pci_resource_start(dev->pdev, PVRDMA_PCI_RESOURCE_REG);
	len = pci_resource_len(dev->pdev, PVRDMA_PCI_RESOURCE_REG);
	dev->regs = ioremap(start, len);
	if (!dev->regs) {
		dev_err(&pdev->dev, "register mapping failed\n");
		ret = -ENOMEM;
		goto err_free_resource;
	}

	/* Setup per-device UAR. */
	dev->driver_uar.index = 0;
	dev->driver_uar.pfn =
		pci_resource_start(dev->pdev, PVRDMA_PCI_RESOURCE_UAR) >>
		PAGE_SHIFT;
	dev->driver_uar.map =
		ioremap(dev->driver_uar.pfn << PAGE_SHIFT, PAGE_SIZE);
	if (!dev->driver_uar.map) {
		dev_err(&pdev->dev, "failed to remap UAR pages\n");
		ret = -ENOMEM;
		goto err_unmap_regs;
	}

	dev->dsr_version = pvrdma_read_reg(dev, PVRDMA_REG_VERSION);
	dev_info(&pdev->dev, "device version %d, driver version %d\n",
		 dev->dsr_version, PVRDMA_VERSION);

	dev->dsr = dma_alloc_coherent(&pdev->dev, sizeof(*dev->dsr),
				      &dev->dsrbase, GFP_KERNEL);
	if (!dev->dsr) {
		dev_err(&pdev->dev, "failed to allocate shared region\n");
		ret = -ENOMEM;
		goto err_uar_unmap;
	}

	/* Setup the shared region */
	memset(dev->dsr, 0, sizeof(*dev->dsr));
	dev->dsr->driver_version = PVRDMA_VERSION;
	dev->dsr->gos_info.gos_bits = sizeof(void *) == 4 ?
		PVRDMA_GOS_BITS_32 :
		PVRDMA_GOS_BITS_64;
	dev->dsr->gos_info.gos_type = PVRDMA_GOS_TYPE_LINUX;
	dev->dsr->gos_info.gos_ver = 1;
	dev->dsr->uar_pfn = dev->driver_uar.pfn;

	/* Command slot. */
	dev->cmd_slot = dma_alloc_coherent(&pdev->dev, PAGE_SIZE,
					   &slot_dma, GFP_KERNEL);
	if (!dev->cmd_slot) {
		ret = -ENOMEM;
		goto err_free_dsr;
	}

	dev->dsr->cmd_slot_dma = (u64)slot_dma;

	/* Response slot. */
	dev->resp_slot = dma_alloc_coherent(&pdev->dev, PAGE_SIZE,
					    &slot_dma, GFP_KERNEL);
	if (!dev->resp_slot) {
		ret = -ENOMEM;
		goto err_free_slots;
	}

	dev->dsr->resp_slot_dma = (u64)slot_dma;

	/* Async event ring */
	dev->dsr->async_ring_pages.num_pages = PVRDMA_NUM_RING_PAGES;
	ret = pvrdma_page_dir_init(dev, &dev->async_pdir,
				   dev->dsr->async_ring_pages.num_pages, true);
	if (ret)
		goto err_free_slots;
	dev->async_ring_state = dev->async_pdir.pages[0];
	dev->dsr->async_ring_pages.pdir_dma = dev->async_pdir.dir_dma;

	/* CQ notification ring */
	dev->dsr->cq_ring_pages.num_pages = PVRDMA_NUM_RING_PAGES;
	ret = pvrdma_page_dir_init(dev, &dev->cq_pdir,
				   dev->dsr->cq_ring_pages.num_pages, true);
	if (ret)
		goto err_free_async_ring;
	dev->cq_ring_state = dev->cq_pdir.pages[0];
	dev->dsr->cq_ring_pages.pdir_dma = dev->cq_pdir.dir_dma;

	/*
	 * Write the PA of the shared region to the device. The writes must be
	 * ordered such that the high bits are written last. When the writes
	 * complete, the device will have filled out the capabilities.
	 */

	pvrdma_write_reg(dev, PVRDMA_REG_DSRLOW, (u32)dev->dsrbase);
	pvrdma_write_reg(dev, PVRDMA_REG_DSRHIGH,
			 (u32)((u64)(dev->dsrbase) >> 32));

	/* Make sure the write is complete before reading status. */
	mb();

	/* The driver supports RoCE V1 and V2. */
	if (!PVRDMA_SUPPORTED(dev)) {
		dev_err(&pdev->dev, "driver needs RoCE v1 or v2 support\n");
		ret = -EFAULT;
		goto err_free_cq_ring;
	}

	/* Paired vmxnet3 will have same bus, slot. But func will be 0 */
	pdev_net = pci_get_slot(pdev->bus, PCI_DEVFN(PCI_SLOT(pdev->devfn), 0));
	if (!pdev_net) {
		dev_err(&pdev->dev, "failed to find paired net device\n");
		ret = -ENODEV;
		goto err_free_cq_ring;
	}

	if (pdev_net->vendor != PCI_VENDOR_ID_VMWARE ||
	    pdev_net->device != PCI_DEVICE_ID_VMWARE_VMXNET3) {
		dev_err(&pdev->dev, "failed to find paired vmxnet3 device\n");
		pci_dev_put(pdev_net);
		ret = -ENODEV;
		goto err_free_cq_ring;
	}

	dev->netdev = pci_get_drvdata(pdev_net);
	pci_dev_put(pdev_net);
	if (!dev->netdev) {
		dev_err(&pdev->dev, "failed to get vmxnet3 device\n");
		ret = -ENODEV;
		goto err_free_cq_ring;
	}

	dev_info(&pdev->dev, "paired device to %s\n", dev->netdev->name);

	/* Interrupt setup */
	ret = pvrdma_alloc_intrs(dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to allocate interrupts\n");
		ret = -ENOMEM;
		goto err_free_cq_ring;
	}

	/* Allocate UAR table. */
	ret = pvrdma_uar_table_init(dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to allocate UAR table\n");
		ret = -ENOMEM;
		goto err_free_intrs;
	}

	/* Allocate GID table */
	dev->sgid_tbl = kcalloc(dev->dsr->caps.gid_tbl_len,
				sizeof(union ib_gid), GFP_KERNEL);
	if (!dev->sgid_tbl) {
		ret = -ENOMEM;
		goto err_free_uar_table;
	}
	dev_dbg(&pdev->dev, "gid table len %d\n", dev->dsr->caps.gid_tbl_len);

	pvrdma_enable_intrs(dev);

	/* Activate pvrdma device */
	pvrdma_write_reg(dev, PVRDMA_REG_CTL, PVRDMA_DEVICE_CTL_ACTIVATE);

	/* Make sure the write is complete before reading status. */
	mb();

	/* Check if device was successfully activated */
	ret = pvrdma_read_reg(dev, PVRDMA_REG_ERR);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to activate device\n");
		ret = -EFAULT;
		goto err_disable_intr;
	}

	/* Register IB device */
	ret = pvrdma_register_device(dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register IB device\n");
		goto err_disable_intr;
	}

	dev->nb_netdev.notifier_call = pvrdma_netdevice_event;
	ret = register_netdevice_notifier(&dev->nb_netdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register netdevice events\n");
		goto err_unreg_ibdev;
	}

	dev_info(&pdev->dev, "attached to device\n");
	return 0;

err_unreg_ibdev:
	ib_unregister_device(&dev->ib_dev);
err_disable_intr:
	pvrdma_disable_intrs(dev);
	kfree(dev->sgid_tbl);
err_free_uar_table:
	pvrdma_uar_table_cleanup(dev);
err_free_intrs:
	pvrdma_free_irq(dev);
	pci_free_irq_vectors(pdev);
err_free_cq_ring:
	pvrdma_page_dir_cleanup(dev, &dev->cq_pdir);
err_free_async_ring:
	pvrdma_page_dir_cleanup(dev, &dev->async_pdir);
err_free_slots:
	pvrdma_free_slots(dev);
err_free_dsr:
	dma_free_coherent(&pdev->dev, sizeof(*dev->dsr), dev->dsr,
			  dev->dsrbase);
err_uar_unmap:
	iounmap(dev->driver_uar.map);
err_unmap_regs:
	iounmap(dev->regs);
err_free_resource:
	pci_release_regions(pdev);
err_disable_pdev:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
err_free_device:
	mutex_lock(&pvrdma_device_list_lock);
	list_del(&dev->device_link);
	mutex_unlock(&pvrdma_device_list_lock);
	ib_dealloc_device(&dev->ib_dev);
	return ret;
}

static void pvrdma_pci_remove(struct pci_dev *pdev)
{
	struct pvrdma_dev *dev = pci_get_drvdata(pdev);

	if (!dev)
		return;

	dev_info(&pdev->dev, "detaching from device\n");

	unregister_netdevice_notifier(&dev->nb_netdev);
	dev->nb_netdev.notifier_call = NULL;

	flush_workqueue(event_wq);

	/* Unregister ib device */
	ib_unregister_device(&dev->ib_dev);

	mutex_lock(&pvrdma_device_list_lock);
	list_del(&dev->device_link);
	mutex_unlock(&pvrdma_device_list_lock);

	pvrdma_disable_intrs(dev);
	pvrdma_free_irq(dev);
	pci_free_irq_vectors(pdev);

	/* Deactivate pvrdma device */
	pvrdma_write_reg(dev, PVRDMA_REG_CTL, PVRDMA_DEVICE_CTL_RESET);
	pvrdma_page_dir_cleanup(dev, &dev->cq_pdir);
	pvrdma_page_dir_cleanup(dev, &dev->async_pdir);
	pvrdma_free_slots(dev);

	iounmap(dev->regs);
	kfree(dev->sgid_tbl);
	kfree(dev->cq_tbl);
	kfree(dev->qp_tbl);
	pvrdma_uar_table_cleanup(dev);
	iounmap(dev->driver_uar.map);

	ib_dealloc_device(&dev->ib_dev);

	/* Free pci resources */
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static const struct pci_device_id pvrdma_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_VMWARE, PCI_DEVICE_ID_VMWARE_PVRDMA), },
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, pvrdma_pci_table);

static struct pci_driver pvrdma_driver = {
	.name		= DRV_NAME,
	.id_table	= pvrdma_pci_table,
	.probe		= pvrdma_pci_probe,
	.remove		= pvrdma_pci_remove,
};

static int __init pvrdma_init(void)
{
	int err;

	event_wq = alloc_ordered_workqueue("pvrdma_event_wq", WQ_MEM_RECLAIM);
	if (!event_wq)
		return -ENOMEM;

	err = pci_register_driver(&pvrdma_driver);
	if (err)
		destroy_workqueue(event_wq);

	return err;
}

static void __exit pvrdma_cleanup(void)
{
	pci_unregister_driver(&pvrdma_driver);

	destroy_workqueue(event_wq);
}

module_init(pvrdma_init);
module_exit(pvrdma_cleanup);

MODULE_AUTHOR("VMware, Inc");
MODULE_DESCRIPTION("VMware Paravirtual RDMA driver");
MODULE_LICENSE("Dual BSD/GPL");
