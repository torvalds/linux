/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
#include <linux/module.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_mad.h>
#include <linux/netdevice.h>
#include <linux/iommu.h>
#include <linux/pci.h>
#include <net/addrconf.h>
#include <linux/idr.h>

#include <linux/qed/qed_chain.h>
#include <linux/qed/qed_if.h>
#include "qedr.h"
#include "verbs.h"
#include <rdma/qedr-abi.h>
#include "qedr_iw_cm.h"

MODULE_DESCRIPTION("QLogic 40G/100G ROCE Driver");
MODULE_AUTHOR("QLogic Corporation");
MODULE_LICENSE("Dual BSD/GPL");

#define QEDR_WQ_MULTIPLIER_DFT	(3)

static void qedr_ib_dispatch_event(struct qedr_dev *dev, u8 port_num,
				   enum ib_event_type type)
{
	struct ib_event ibev;

	ibev.device = &dev->ibdev;
	ibev.element.port_num = port_num;
	ibev.event = type;

	ib_dispatch_event(&ibev);
}

static enum rdma_link_layer qedr_link_layer(struct ib_device *device,
					    u8 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

static void qedr_get_dev_fw_str(struct ib_device *ibdev, char *str)
{
	struct qedr_dev *qedr = get_qedr_dev(ibdev);
	u32 fw_ver = (u32)qedr->attr.fw_ver;

	snprintf(str, IB_FW_VERSION_NAME_MAX, "%d. %d. %d. %d",
		 (fw_ver >> 24) & 0xFF, (fw_ver >> 16) & 0xFF,
		 (fw_ver >> 8) & 0xFF, fw_ver & 0xFF);
}

static struct net_device *qedr_get_netdev(struct ib_device *dev, u8 port_num)
{
	struct qedr_dev *qdev;

	qdev = get_qedr_dev(dev);
	dev_hold(qdev->ndev);

	/* The HW vendor's device driver must guarantee
	 * that this function returns NULL before the net device reaches
	 * NETDEV_UNREGISTER_FINAL state.
	 */
	return qdev->ndev;
}

static int qedr_roce_port_immutable(struct ib_device *ibdev, u8 port_num,
				    struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	err = qedr_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE |
	    RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;
	immutable->max_mad_size = IB_MGMT_MAD_SIZE;

	return 0;
}

static int qedr_iw_port_immutable(struct ib_device *ibdev, u8 port_num,
				  struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	err = qedr_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = 1;
	immutable->gid_tbl_len = 1;
	immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;
	immutable->max_mad_size = 0;

	return 0;
}

static int qedr_iw_register_device(struct qedr_dev *dev)
{
	dev->ibdev.node_type = RDMA_NODE_RNIC;
	dev->ibdev.query_gid = qedr_iw_query_gid;

	dev->ibdev.get_port_immutable = qedr_iw_port_immutable;

	dev->ibdev.iwcm = kzalloc(sizeof(*dev->ibdev.iwcm), GFP_KERNEL);
	if (!dev->ibdev.iwcm)
		return -ENOMEM;

	dev->ibdev.iwcm->connect = qedr_iw_connect;
	dev->ibdev.iwcm->accept = qedr_iw_accept;
	dev->ibdev.iwcm->reject = qedr_iw_reject;
	dev->ibdev.iwcm->create_listen = qedr_iw_create_listen;
	dev->ibdev.iwcm->destroy_listen = qedr_iw_destroy_listen;
	dev->ibdev.iwcm->add_ref = qedr_iw_qp_add_ref;
	dev->ibdev.iwcm->rem_ref = qedr_iw_qp_rem_ref;
	dev->ibdev.iwcm->get_qp = qedr_iw_get_qp;

	memcpy(dev->ibdev.iwcm->ifname,
	       dev->ndev->name, sizeof(dev->ibdev.iwcm->ifname));

	return 0;
}

static void qedr_roce_register_device(struct qedr_dev *dev)
{
	dev->ibdev.node_type = RDMA_NODE_IB_CA;
	dev->ibdev.query_gid = qedr_query_gid;

	dev->ibdev.add_gid = qedr_add_gid;
	dev->ibdev.del_gid = qedr_del_gid;

	dev->ibdev.get_port_immutable = qedr_roce_port_immutable;
}

static int qedr_register_device(struct qedr_dev *dev)
{
	int rc;

	strlcpy(dev->ibdev.name, "qedr%d", IB_DEVICE_NAME_MAX);

	dev->ibdev.node_guid = dev->attr.node_guid;
	memcpy(dev->ibdev.node_desc, QEDR_NODE_DESC, sizeof(QEDR_NODE_DESC));
	dev->ibdev.owner = THIS_MODULE;
	dev->ibdev.uverbs_abi_ver = QEDR_ABI_VERSION;

	dev->ibdev.uverbs_cmd_mask = QEDR_UVERBS(GET_CONTEXT) |
				     QEDR_UVERBS(QUERY_DEVICE) |
				     QEDR_UVERBS(QUERY_PORT) |
				     QEDR_UVERBS(ALLOC_PD) |
				     QEDR_UVERBS(DEALLOC_PD) |
				     QEDR_UVERBS(CREATE_COMP_CHANNEL) |
				     QEDR_UVERBS(CREATE_CQ) |
				     QEDR_UVERBS(RESIZE_CQ) |
				     QEDR_UVERBS(DESTROY_CQ) |
				     QEDR_UVERBS(REQ_NOTIFY_CQ) |
				     QEDR_UVERBS(CREATE_QP) |
				     QEDR_UVERBS(MODIFY_QP) |
				     QEDR_UVERBS(QUERY_QP) |
				     QEDR_UVERBS(DESTROY_QP) |
				     QEDR_UVERBS(REG_MR) |
				     QEDR_UVERBS(DEREG_MR) |
				     QEDR_UVERBS(POLL_CQ) |
				     QEDR_UVERBS(POST_SEND) |
				     QEDR_UVERBS(POST_RECV);

	if (IS_IWARP(dev)) {
		rc = qedr_iw_register_device(dev);
		if (rc)
			return rc;
	} else {
		qedr_roce_register_device(dev);
	}

	dev->ibdev.phys_port_cnt = 1;
	dev->ibdev.num_comp_vectors = dev->num_cnq;

	dev->ibdev.query_device = qedr_query_device;
	dev->ibdev.query_port = qedr_query_port;
	dev->ibdev.modify_port = qedr_modify_port;

	dev->ibdev.alloc_ucontext = qedr_alloc_ucontext;
	dev->ibdev.dealloc_ucontext = qedr_dealloc_ucontext;
	dev->ibdev.mmap = qedr_mmap;

	dev->ibdev.alloc_pd = qedr_alloc_pd;
	dev->ibdev.dealloc_pd = qedr_dealloc_pd;

	dev->ibdev.create_cq = qedr_create_cq;
	dev->ibdev.destroy_cq = qedr_destroy_cq;
	dev->ibdev.resize_cq = qedr_resize_cq;
	dev->ibdev.req_notify_cq = qedr_arm_cq;

	dev->ibdev.create_qp = qedr_create_qp;
	dev->ibdev.modify_qp = qedr_modify_qp;
	dev->ibdev.query_qp = qedr_query_qp;
	dev->ibdev.destroy_qp = qedr_destroy_qp;

	dev->ibdev.query_pkey = qedr_query_pkey;

	dev->ibdev.create_ah = qedr_create_ah;
	dev->ibdev.destroy_ah = qedr_destroy_ah;

	dev->ibdev.get_dma_mr = qedr_get_dma_mr;
	dev->ibdev.dereg_mr = qedr_dereg_mr;
	dev->ibdev.reg_user_mr = qedr_reg_user_mr;
	dev->ibdev.alloc_mr = qedr_alloc_mr;
	dev->ibdev.map_mr_sg = qedr_map_mr_sg;

	dev->ibdev.poll_cq = qedr_poll_cq;
	dev->ibdev.post_send = qedr_post_send;
	dev->ibdev.post_recv = qedr_post_recv;

	dev->ibdev.process_mad = qedr_process_mad;

	dev->ibdev.get_netdev = qedr_get_netdev;

	dev->ibdev.dev.parent = &dev->pdev->dev;

	dev->ibdev.get_link_layer = qedr_link_layer;
	dev->ibdev.get_dev_fw_str = qedr_get_dev_fw_str;

	return ib_register_device(&dev->ibdev, NULL);
}

/* This function allocates fast-path status block memory */
static int qedr_alloc_mem_sb(struct qedr_dev *dev,
			     struct qed_sb_info *sb_info, u16 sb_id)
{
	struct status_block_e4 *sb_virt;
	dma_addr_t sb_phys;
	int rc;

	sb_virt = dma_alloc_coherent(&dev->pdev->dev,
				     sizeof(*sb_virt), &sb_phys, GFP_KERNEL);
	if (!sb_virt)
		return -ENOMEM;

	rc = dev->ops->common->sb_init(dev->cdev, sb_info,
				       sb_virt, sb_phys, sb_id,
				       QED_SB_TYPE_CNQ);
	if (rc) {
		pr_err("Status block initialization failed\n");
		dma_free_coherent(&dev->pdev->dev, sizeof(*sb_virt),
				  sb_virt, sb_phys);
		return rc;
	}

	return 0;
}

static void qedr_free_mem_sb(struct qedr_dev *dev,
			     struct qed_sb_info *sb_info, int sb_id)
{
	if (sb_info->sb_virt) {
		dev->ops->common->sb_release(dev->cdev, sb_info, sb_id);
		dma_free_coherent(&dev->pdev->dev, sizeof(*sb_info->sb_virt),
				  (void *)sb_info->sb_virt, sb_info->sb_phys);
	}
}

static void qedr_free_resources(struct qedr_dev *dev)
{
	int i;

	if (IS_IWARP(dev))
		destroy_workqueue(dev->iwarp_wq);

	for (i = 0; i < dev->num_cnq; i++) {
		qedr_free_mem_sb(dev, &dev->sb_array[i], dev->sb_start + i);
		dev->ops->common->chain_free(dev->cdev, &dev->cnq_array[i].pbl);
	}

	kfree(dev->cnq_array);
	kfree(dev->sb_array);
	kfree(dev->sgid_tbl);
}

static int qedr_alloc_resources(struct qedr_dev *dev)
{
	struct qedr_cnq *cnq;
	__le16 *cons_pi;
	u16 n_entries;
	int i, rc;

	dev->sgid_tbl = kzalloc(sizeof(union ib_gid) *
				QEDR_MAX_SGID, GFP_KERNEL);
	if (!dev->sgid_tbl)
		return -ENOMEM;

	spin_lock_init(&dev->sgid_lock);

	if (IS_IWARP(dev)) {
		spin_lock_init(&dev->idr_lock);
		idr_init(&dev->qpidr);
		dev->iwarp_wq = create_singlethread_workqueue("qedr_iwarpq");
	}

	/* Allocate Status blocks for CNQ */
	dev->sb_array = kcalloc(dev->num_cnq, sizeof(*dev->sb_array),
				GFP_KERNEL);
	if (!dev->sb_array) {
		rc = -ENOMEM;
		goto err1;
	}

	dev->cnq_array = kcalloc(dev->num_cnq,
				 sizeof(*dev->cnq_array), GFP_KERNEL);
	if (!dev->cnq_array) {
		rc = -ENOMEM;
		goto err2;
	}

	dev->sb_start = dev->ops->rdma_get_start_sb(dev->cdev);

	/* Allocate CNQ PBLs */
	n_entries = min_t(u32, QED_RDMA_MAX_CNQ_SIZE, QEDR_ROCE_MAX_CNQ_SIZE);
	for (i = 0; i < dev->num_cnq; i++) {
		cnq = &dev->cnq_array[i];

		rc = qedr_alloc_mem_sb(dev, &dev->sb_array[i],
				       dev->sb_start + i);
		if (rc)
			goto err3;

		rc = dev->ops->common->chain_alloc(dev->cdev,
						   QED_CHAIN_USE_TO_CONSUME,
						   QED_CHAIN_MODE_PBL,
						   QED_CHAIN_CNT_TYPE_U16,
						   n_entries,
						   sizeof(struct regpair *),
						   &cnq->pbl, NULL);
		if (rc)
			goto err4;

		cnq->dev = dev;
		cnq->sb = &dev->sb_array[i];
		cons_pi = dev->sb_array[i].sb_virt->pi_array;
		cnq->hw_cons_ptr = &cons_pi[QED_ROCE_PROTOCOL_INDEX];
		cnq->index = i;
		sprintf(cnq->name, "qedr%d@pci:%s", i, pci_name(dev->pdev));

		DP_DEBUG(dev, QEDR_MSG_INIT, "cnq[%d].cons=%d\n",
			 i, qed_chain_get_cons_idx(&cnq->pbl));
	}

	return 0;
err4:
	qedr_free_mem_sb(dev, &dev->sb_array[i], dev->sb_start + i);
err3:
	for (--i; i >= 0; i--) {
		dev->ops->common->chain_free(dev->cdev, &dev->cnq_array[i].pbl);
		qedr_free_mem_sb(dev, &dev->sb_array[i], dev->sb_start + i);
	}
	kfree(dev->cnq_array);
err2:
	kfree(dev->sb_array);
err1:
	kfree(dev->sgid_tbl);
	return rc;
}

/* QEDR sysfs interface */
static ssize_t show_rev(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct qedr_dev *dev = dev_get_drvdata(device);

	return scnprintf(buf, PAGE_SIZE, "0x%x\n", dev->pdev->vendor);
}

static ssize_t show_hca_type(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", "HCA_TYPE_TO_SET");
}

static DEVICE_ATTR(hw_rev, S_IRUGO, show_rev, NULL);
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca_type, NULL);

static struct device_attribute *qedr_attributes[] = {
	&dev_attr_hw_rev,
	&dev_attr_hca_type
};

static void qedr_remove_sysfiles(struct qedr_dev *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qedr_attributes); i++)
		device_remove_file(&dev->ibdev.dev, qedr_attributes[i]);
}

static void qedr_pci_set_atomic(struct qedr_dev *dev, struct pci_dev *pdev)
{
	int rc = pci_enable_atomic_ops_to_root(pdev,
					       PCI_EXP_DEVCAP2_ATOMIC_COMP64);

	if (rc) {
		dev->atomic_cap = IB_ATOMIC_NONE;
		DP_DEBUG(dev, QEDR_MSG_INIT, "Atomic capability disabled\n");
	} else {
		dev->atomic_cap = IB_ATOMIC_GLOB;
		DP_DEBUG(dev, QEDR_MSG_INIT, "Atomic capability enabled\n");
	}
}

static const struct qed_rdma_ops *qed_ops;

#define HILO_U64(hi, lo)		((((u64)(hi)) << 32) + (lo))

static irqreturn_t qedr_irq_handler(int irq, void *handle)
{
	u16 hw_comp_cons, sw_comp_cons;
	struct qedr_cnq *cnq = handle;
	struct regpair *cq_handle;
	struct qedr_cq *cq;

	qed_sb_ack(cnq->sb, IGU_INT_DISABLE, 0);

	qed_sb_update_sb_idx(cnq->sb);

	hw_comp_cons = le16_to_cpu(*cnq->hw_cons_ptr);
	sw_comp_cons = qed_chain_get_cons_idx(&cnq->pbl);

	/* Align protocol-index and chain reads */
	rmb();

	while (sw_comp_cons != hw_comp_cons) {
		cq_handle = (struct regpair *)qed_chain_consume(&cnq->pbl);
		cq = (struct qedr_cq *)(uintptr_t)HILO_U64(cq_handle->hi,
				cq_handle->lo);

		if (cq == NULL) {
			DP_ERR(cnq->dev,
			       "Received NULL CQ cq_handle->hi=%d cq_handle->lo=%d sw_comp_cons=%d hw_comp_cons=%d\n",
			       cq_handle->hi, cq_handle->lo, sw_comp_cons,
			       hw_comp_cons);

			break;
		}

		if (cq->sig != QEDR_CQ_MAGIC_NUMBER) {
			DP_ERR(cnq->dev,
			       "Problem with cq signature, cq_handle->hi=%d ch_handle->lo=%d cq=%p\n",
			       cq_handle->hi, cq_handle->lo, cq);
			break;
		}

		cq->arm_flags = 0;

		if (!cq->destroyed && cq->ibcq.comp_handler)
			(*cq->ibcq.comp_handler)
				(&cq->ibcq, cq->ibcq.cq_context);

		/* The CQ's CNQ notification counter is checked before
		 * destroying the CQ in a busy-wait loop that waits for all of
		 * the CQ's CNQ interrupts to be processed. It is increased
		 * here, only after the completion handler, to ensure that the
		 * the handler is not running when the CQ is destroyed.
		 */
		cq->cnq_notif++;

		sw_comp_cons = qed_chain_get_cons_idx(&cnq->pbl);

		cnq->n_comp++;
	}

	qed_ops->rdma_cnq_prod_update(cnq->dev->rdma_ctx, cnq->index,
				      sw_comp_cons);

	qed_sb_ack(cnq->sb, IGU_INT_ENABLE, 1);

	return IRQ_HANDLED;
}

static void qedr_sync_free_irqs(struct qedr_dev *dev)
{
	u32 vector;
	int i;

	for (i = 0; i < dev->int_info.used_cnt; i++) {
		if (dev->int_info.msix_cnt) {
			vector = dev->int_info.msix[i * dev->num_hwfns].vector;
			synchronize_irq(vector);
			free_irq(vector, &dev->cnq_array[i]);
		}
	}

	dev->int_info.used_cnt = 0;
}

static int qedr_req_msix_irqs(struct qedr_dev *dev)
{
	int i, rc = 0;

	if (dev->num_cnq > dev->int_info.msix_cnt) {
		DP_ERR(dev,
		       "Interrupt mismatch: %d CNQ queues > %d MSI-x vectors\n",
		       dev->num_cnq, dev->int_info.msix_cnt);
		return -EINVAL;
	}

	for (i = 0; i < dev->num_cnq; i++) {
		rc = request_irq(dev->int_info.msix[i * dev->num_hwfns].vector,
				 qedr_irq_handler, 0, dev->cnq_array[i].name,
				 &dev->cnq_array[i]);
		if (rc) {
			DP_ERR(dev, "Request cnq %d irq failed\n", i);
			qedr_sync_free_irqs(dev);
		} else {
			DP_DEBUG(dev, QEDR_MSG_INIT,
				 "Requested cnq irq for %s [entry %d]. Cookie is at %p\n",
				 dev->cnq_array[i].name, i,
				 &dev->cnq_array[i]);
			dev->int_info.used_cnt++;
		}
	}

	return rc;
}

static int qedr_setup_irqs(struct qedr_dev *dev)
{
	int rc;

	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr_setup_irqs\n");

	/* Learn Interrupt configuration */
	rc = dev->ops->rdma_set_rdma_int(dev->cdev, dev->num_cnq);
	if (rc < 0)
		return rc;

	rc = dev->ops->rdma_get_rdma_int(dev->cdev, &dev->int_info);
	if (rc) {
		DP_DEBUG(dev, QEDR_MSG_INIT, "get_rdma_int failed\n");
		return rc;
	}

	if (dev->int_info.msix_cnt) {
		DP_DEBUG(dev, QEDR_MSG_INIT, "rdma msix_cnt = %d\n",
			 dev->int_info.msix_cnt);
		rc = qedr_req_msix_irqs(dev);
		if (rc)
			return rc;
	}

	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr_setup_irqs succeeded\n");

	return 0;
}

static int qedr_set_device_attr(struct qedr_dev *dev)
{
	struct qed_rdma_device *qed_attr;
	struct qedr_device_attr *attr;
	u32 page_size;

	/* Part 1 - query core capabilities */
	qed_attr = dev->ops->rdma_query_device(dev->rdma_ctx);

	/* Part 2 - check capabilities */
	page_size = ~dev->attr.page_size_caps + 1;
	if (page_size > PAGE_SIZE) {
		DP_ERR(dev,
		       "Kernel PAGE_SIZE is %ld which is smaller than minimum page size (%d) required by qedr\n",
		       PAGE_SIZE, page_size);
		return -ENODEV;
	}

	/* Part 3 - copy and update capabilities */
	attr = &dev->attr;
	attr->vendor_id = qed_attr->vendor_id;
	attr->vendor_part_id = qed_attr->vendor_part_id;
	attr->hw_ver = qed_attr->hw_ver;
	attr->fw_ver = qed_attr->fw_ver;
	attr->node_guid = qed_attr->node_guid;
	attr->sys_image_guid = qed_attr->sys_image_guid;
	attr->max_cnq = qed_attr->max_cnq;
	attr->max_sge = qed_attr->max_sge;
	attr->max_inline = qed_attr->max_inline;
	attr->max_sqe = min_t(u32, qed_attr->max_wqe, QEDR_MAX_SQE);
	attr->max_rqe = min_t(u32, qed_attr->max_wqe, QEDR_MAX_RQE);
	attr->max_qp_resp_rd_atomic_resc = qed_attr->max_qp_resp_rd_atomic_resc;
	attr->max_qp_req_rd_atomic_resc = qed_attr->max_qp_req_rd_atomic_resc;
	attr->max_dev_resp_rd_atomic_resc =
	    qed_attr->max_dev_resp_rd_atomic_resc;
	attr->max_cq = qed_attr->max_cq;
	attr->max_qp = qed_attr->max_qp;
	attr->max_mr = qed_attr->max_mr;
	attr->max_mr_size = qed_attr->max_mr_size;
	attr->max_cqe = min_t(u64, qed_attr->max_cqe, QEDR_MAX_CQES);
	attr->max_mw = qed_attr->max_mw;
	attr->max_fmr = qed_attr->max_fmr;
	attr->max_mr_mw_fmr_pbl = qed_attr->max_mr_mw_fmr_pbl;
	attr->max_mr_mw_fmr_size = qed_attr->max_mr_mw_fmr_size;
	attr->max_pd = qed_attr->max_pd;
	attr->max_ah = qed_attr->max_ah;
	attr->max_pkey = qed_attr->max_pkey;
	attr->max_srq = qed_attr->max_srq;
	attr->max_srq_wr = qed_attr->max_srq_wr;
	attr->dev_caps = qed_attr->dev_caps;
	attr->page_size_caps = qed_attr->page_size_caps;
	attr->dev_ack_delay = qed_attr->dev_ack_delay;
	attr->reserved_lkey = qed_attr->reserved_lkey;
	attr->bad_pkey_counter = qed_attr->bad_pkey_counter;
	attr->max_stats_queues = qed_attr->max_stats_queues;

	return 0;
}

static void qedr_unaffiliated_event(void *context, u8 event_code)
{
	pr_err("unaffiliated event not implemented yet\n");
}

static void qedr_affiliated_event(void *context, u8 e_code, void *fw_handle)
{
#define EVENT_TYPE_NOT_DEFINED	0
#define EVENT_TYPE_CQ		1
#define EVENT_TYPE_QP		2
	struct qedr_dev *dev = (struct qedr_dev *)context;
	struct regpair *async_handle = (struct regpair *)fw_handle;
	u64 roce_handle64 = ((u64) async_handle->hi << 32) + async_handle->lo;
	u8 event_type = EVENT_TYPE_NOT_DEFINED;
	struct ib_event event;
	struct ib_cq *ibcq;
	struct ib_qp *ibqp;
	struct qedr_cq *cq;
	struct qedr_qp *qp;

	switch (e_code) {
	case ROCE_ASYNC_EVENT_CQ_OVERFLOW_ERR:
		event.event = IB_EVENT_CQ_ERR;
		event_type = EVENT_TYPE_CQ;
		break;
	case ROCE_ASYNC_EVENT_SQ_DRAINED:
		event.event = IB_EVENT_SQ_DRAINED;
		event_type = EVENT_TYPE_QP;
		break;
	case ROCE_ASYNC_EVENT_QP_CATASTROPHIC_ERR:
		event.event = IB_EVENT_QP_FATAL;
		event_type = EVENT_TYPE_QP;
		break;
	case ROCE_ASYNC_EVENT_LOCAL_INVALID_REQUEST_ERR:
		event.event = IB_EVENT_QP_REQ_ERR;
		event_type = EVENT_TYPE_QP;
		break;
	case ROCE_ASYNC_EVENT_LOCAL_ACCESS_ERR:
		event.event = IB_EVENT_QP_ACCESS_ERR;
		event_type = EVENT_TYPE_QP;
		break;
	default:
		DP_ERR(dev, "unsupported event %d on handle=%llx\n", e_code,
		       roce_handle64);
	}

	switch (event_type) {
	case EVENT_TYPE_CQ:
		cq = (struct qedr_cq *)(uintptr_t)roce_handle64;
		if (cq) {
			ibcq = &cq->ibcq;
			if (ibcq->event_handler) {
				event.device = ibcq->device;
				event.element.cq = ibcq;
				ibcq->event_handler(&event, ibcq->cq_context);
			}
		} else {
			WARN(1,
			     "Error: CQ event with NULL pointer ibcq. Handle=%llx\n",
			     roce_handle64);
		}
		DP_ERR(dev, "CQ event %d on hanlde %p\n", e_code, cq);
		break;
	case EVENT_TYPE_QP:
		qp = (struct qedr_qp *)(uintptr_t)roce_handle64;
		if (qp) {
			ibqp = &qp->ibqp;
			if (ibqp->event_handler) {
				event.device = ibqp->device;
				event.element.qp = ibqp;
				ibqp->event_handler(&event, ibqp->qp_context);
			}
		} else {
			WARN(1,
			     "Error: QP event with NULL pointer ibqp. Handle=%llx\n",
			     roce_handle64);
		}
		DP_ERR(dev, "QP event %d on hanlde %p\n", e_code, qp);
		break;
	default:
		break;
	}
}

static int qedr_init_hw(struct qedr_dev *dev)
{
	struct qed_rdma_add_user_out_params out_params;
	struct qed_rdma_start_in_params *in_params;
	struct qed_rdma_cnq_params *cur_pbl;
	struct qed_rdma_events events;
	dma_addr_t p_phys_table;
	u32 page_cnt;
	int rc = 0;
	int i;

	in_params =  kzalloc(sizeof(*in_params), GFP_KERNEL);
	if (!in_params) {
		rc = -ENOMEM;
		goto out;
	}

	in_params->desired_cnq = dev->num_cnq;
	for (i = 0; i < dev->num_cnq; i++) {
		cur_pbl = &in_params->cnq_pbl_list[i];

		page_cnt = qed_chain_get_page_cnt(&dev->cnq_array[i].pbl);
		cur_pbl->num_pbl_pages = page_cnt;

		p_phys_table = qed_chain_get_pbl_phys(&dev->cnq_array[i].pbl);
		cur_pbl->pbl_ptr = (u64)p_phys_table;
	}

	events.affiliated_event = qedr_affiliated_event;
	events.unaffiliated_event = qedr_unaffiliated_event;
	events.context = dev;

	in_params->events = &events;
	in_params->cq_mode = QED_RDMA_CQ_MODE_32_BITS;
	in_params->max_mtu = dev->ndev->mtu;
	dev->iwarp_max_mtu = dev->ndev->mtu;
	ether_addr_copy(&in_params->mac_addr[0], dev->ndev->dev_addr);

	rc = dev->ops->rdma_init(dev->cdev, in_params);
	if (rc)
		goto out;

	rc = dev->ops->rdma_add_user(dev->rdma_ctx, &out_params);
	if (rc)
		goto out;

	dev->db_addr = (void __iomem *)(uintptr_t)out_params.dpi_addr;
	dev->db_phys_addr = out_params.dpi_phys_addr;
	dev->db_size = out_params.dpi_size;
	dev->dpi = out_params.dpi;

	rc = qedr_set_device_attr(dev);
out:
	kfree(in_params);
	if (rc)
		DP_ERR(dev, "Init HW Failed rc = %d\n", rc);

	return rc;
}

static void qedr_stop_hw(struct qedr_dev *dev)
{
	dev->ops->rdma_remove_user(dev->rdma_ctx, dev->dpi);
	dev->ops->rdma_stop(dev->rdma_ctx);
}

static struct qedr_dev *qedr_add(struct qed_dev *cdev, struct pci_dev *pdev,
				 struct net_device *ndev)
{
	struct qed_dev_rdma_info dev_info;
	struct qedr_dev *dev;
	int rc = 0, i;

	dev = (struct qedr_dev *)ib_alloc_device(sizeof(*dev));
	if (!dev) {
		pr_err("Unable to allocate ib device\n");
		return NULL;
	}

	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr add device called\n");

	dev->pdev = pdev;
	dev->ndev = ndev;
	dev->cdev = cdev;

	qed_ops = qed_get_rdma_ops();
	if (!qed_ops) {
		DP_ERR(dev, "Failed to get qed roce operations\n");
		goto init_err;
	}

	dev->ops = qed_ops;
	rc = qed_ops->fill_dev_info(cdev, &dev_info);
	if (rc)
		goto init_err;

	dev->user_dpm_enabled = dev_info.user_dpm_enabled;
	dev->rdma_type = dev_info.rdma_type;
	dev->num_hwfns = dev_info.common.num_hwfns;
	dev->rdma_ctx = dev->ops->rdma_get_rdma_ctx(cdev);

	dev->num_cnq = dev->ops->rdma_get_min_cnq_msix(cdev);
	if (!dev->num_cnq) {
		DP_ERR(dev, "Failed. At least one CNQ is required.\n");
		rc = -ENOMEM;
		goto init_err;
	}

	dev->wq_multiplier = QEDR_WQ_MULTIPLIER_DFT;

	qedr_pci_set_atomic(dev, pdev);

	rc = qedr_alloc_resources(dev);
	if (rc)
		goto init_err;

	rc = qedr_init_hw(dev);
	if (rc)
		goto alloc_err;

	rc = qedr_setup_irqs(dev);
	if (rc)
		goto irq_err;

	rc = qedr_register_device(dev);
	if (rc) {
		DP_ERR(dev, "Unable to allocate register device\n");
		goto reg_err;
	}

	for (i = 0; i < ARRAY_SIZE(qedr_attributes); i++)
		if (device_create_file(&dev->ibdev.dev, qedr_attributes[i]))
			goto sysfs_err;

	if (!test_and_set_bit(QEDR_ENET_STATE_BIT, &dev->enet_state))
		qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_PORT_ACTIVE);

	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr driver loaded successfully\n");
	return dev;

sysfs_err:
	ib_unregister_device(&dev->ibdev);
reg_err:
	qedr_sync_free_irqs(dev);
irq_err:
	qedr_stop_hw(dev);
alloc_err:
	qedr_free_resources(dev);
init_err:
	ib_dealloc_device(&dev->ibdev);
	DP_ERR(dev, "qedr driver load failed rc=%d\n", rc);

	return NULL;
}

static void qedr_remove(struct qedr_dev *dev)
{
	/* First unregister with stack to stop all the active traffic
	 * of the registered clients.
	 */
	qedr_remove_sysfiles(dev);
	ib_unregister_device(&dev->ibdev);

	qedr_stop_hw(dev);
	qedr_sync_free_irqs(dev);
	qedr_free_resources(dev);
	ib_dealloc_device(&dev->ibdev);
}

static void qedr_close(struct qedr_dev *dev)
{
	if (test_and_clear_bit(QEDR_ENET_STATE_BIT, &dev->enet_state))
		qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_PORT_ERR);
}

static void qedr_shutdown(struct qedr_dev *dev)
{
	qedr_close(dev);
	qedr_remove(dev);
}

static void qedr_open(struct qedr_dev *dev)
{
	if (!test_and_set_bit(QEDR_ENET_STATE_BIT, &dev->enet_state))
		qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_PORT_ACTIVE);
}

static void qedr_mac_address_change(struct qedr_dev *dev)
{
	union ib_gid *sgid = &dev->sgid_tbl[0];
	u8 guid[8], mac_addr[6];
	int rc;

	/* Update SGID */
	ether_addr_copy(&mac_addr[0], dev->ndev->dev_addr);
	guid[0] = mac_addr[0] ^ 2;
	guid[1] = mac_addr[1];
	guid[2] = mac_addr[2];
	guid[3] = 0xff;
	guid[4] = 0xfe;
	guid[5] = mac_addr[3];
	guid[6] = mac_addr[4];
	guid[7] = mac_addr[5];
	sgid->global.subnet_prefix = cpu_to_be64(0xfe80000000000000LL);
	memcpy(&sgid->raw[8], guid, sizeof(guid));

	/* Update LL2 */
	rc = dev->ops->ll2_set_mac_filter(dev->cdev,
					  dev->gsi_ll2_mac_address,
					  dev->ndev->dev_addr);

	ether_addr_copy(dev->gsi_ll2_mac_address, dev->ndev->dev_addr);

	qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_GID_CHANGE);

	if (rc)
		DP_ERR(dev, "Error updating mac filter\n");
}

/* event handling via NIC driver ensures that all the NIC specific
 * initialization done before RoCE driver notifies
 * event to stack.
 */
static void qedr_notify(struct qedr_dev *dev, enum qede_rdma_event event)
{
	switch (event) {
	case QEDE_UP:
		qedr_open(dev);
		break;
	case QEDE_DOWN:
		qedr_close(dev);
		break;
	case QEDE_CLOSE:
		qedr_shutdown(dev);
		break;
	case QEDE_CHANGE_ADDR:
		qedr_mac_address_change(dev);
		break;
	default:
		pr_err("Event not supported\n");
	}
}

static struct qedr_driver qedr_drv = {
	.name = "qedr_driver",
	.add = qedr_add,
	.remove = qedr_remove,
	.notify = qedr_notify,
};

static int __init qedr_init_module(void)
{
	return qede_rdma_register_driver(&qedr_drv);
}

static void __exit qedr_exit_module(void)
{
	qede_rdma_unregister_driver(&qedr_drv);
}

module_init(qedr_init_module);
module_exit(qedr_exit_module);
