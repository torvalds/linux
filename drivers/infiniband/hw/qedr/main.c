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

static void qedr_ib_dispatch_event(struct qedr_dev *dev, u32 port_num,
				   enum ib_event_type type)
{
	struct ib_event ibev;

	ibev.device = &dev->ibdev;
	ibev.element.port_num = port_num;
	ibev.event = type;

	ib_dispatch_event(&ibev);
}

static enum rdma_link_layer qedr_link_layer(struct ib_device *device,
					    u32 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

static void qedr_get_dev_fw_str(struct ib_device *ibdev, char *str)
{
	struct qedr_dev *qedr = get_qedr_dev(ibdev);
	u32 fw_ver = (u32)qedr->attr.fw_ver;

	snprintf(str, IB_FW_VERSION_NAME_MAX, "%d.%d.%d.%d",
		 (fw_ver >> 24) & 0xFF, (fw_ver >> 16) & 0xFF,
		 (fw_ver >> 8) & 0xFF, fw_ver & 0xFF);
}

static int qedr_roce_port_immutable(struct ib_device *ibdev, u32 port_num,
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

static int qedr_iw_port_immutable(struct ib_device *ibdev, u32 port_num,
				  struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	err = qedr_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->gid_tbl_len = 1;
	immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;
	immutable->max_mad_size = 0;

	return 0;
}

/* QEDR sysfs interface */
static ssize_t hw_rev_show(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	struct qedr_dev *dev =
		rdma_device_to_drv_device(device, struct qedr_dev, ibdev);

	return sysfs_emit(buf, "0x%x\n", dev->attr.hw_ver);
}
static DEVICE_ATTR_RO(hw_rev);

static ssize_t hca_type_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct qedr_dev *dev =
		rdma_device_to_drv_device(device, struct qedr_dev, ibdev);

	return sysfs_emit(buf, "FastLinQ QL%x %s\n", dev->pdev->device,
			  rdma_protocol_iwarp(&dev->ibdev, 1) ? "iWARP" :
								"RoCE");
}
static DEVICE_ATTR_RO(hca_type);

static struct attribute *qedr_attributes[] = {
	&dev_attr_hw_rev.attr,
	&dev_attr_hca_type.attr,
	NULL
};

static const struct attribute_group qedr_attr_group = {
	.attrs = qedr_attributes,
};

static const struct ib_device_ops qedr_iw_dev_ops = {
	.get_port_immutable = qedr_iw_port_immutable,
	.iw_accept = qedr_iw_accept,
	.iw_add_ref = qedr_iw_qp_add_ref,
	.iw_connect = qedr_iw_connect,
	.iw_create_listen = qedr_iw_create_listen,
	.iw_destroy_listen = qedr_iw_destroy_listen,
	.iw_get_qp = qedr_iw_get_qp,
	.iw_reject = qedr_iw_reject,
	.iw_rem_ref = qedr_iw_qp_rem_ref,
	.query_gid = qedr_iw_query_gid,
};

static int qedr_iw_register_device(struct qedr_dev *dev)
{
	dev->ibdev.node_type = RDMA_NODE_RNIC;

	ib_set_device_ops(&dev->ibdev, &qedr_iw_dev_ops);

	memcpy(dev->ibdev.iw_ifname,
	       dev->ndev->name, sizeof(dev->ibdev.iw_ifname));

	return 0;
}

static const struct ib_device_ops qedr_roce_dev_ops = {
	.alloc_xrcd = qedr_alloc_xrcd,
	.dealloc_xrcd = qedr_dealloc_xrcd,
	.get_port_immutable = qedr_roce_port_immutable,
	.query_pkey = qedr_query_pkey,
};

static void qedr_roce_register_device(struct qedr_dev *dev)
{
	dev->ibdev.node_type = RDMA_NODE_IB_CA;

	ib_set_device_ops(&dev->ibdev, &qedr_roce_dev_ops);
}

static const struct ib_device_ops qedr_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_QEDR,
	.uverbs_abi_ver = QEDR_ABI_VERSION,

	.alloc_mr = qedr_alloc_mr,
	.alloc_pd = qedr_alloc_pd,
	.alloc_ucontext = qedr_alloc_ucontext,
	.create_ah = qedr_create_ah,
	.create_cq = qedr_create_cq,
	.create_qp = qedr_create_qp,
	.create_srq = qedr_create_srq,
	.dealloc_pd = qedr_dealloc_pd,
	.dealloc_ucontext = qedr_dealloc_ucontext,
	.dereg_mr = qedr_dereg_mr,
	.destroy_ah = qedr_destroy_ah,
	.destroy_cq = qedr_destroy_cq,
	.destroy_qp = qedr_destroy_qp,
	.destroy_srq = qedr_destroy_srq,
	.device_group = &qedr_attr_group,
	.get_dev_fw_str = qedr_get_dev_fw_str,
	.get_dma_mr = qedr_get_dma_mr,
	.get_link_layer = qedr_link_layer,
	.map_mr_sg = qedr_map_mr_sg,
	.mmap = qedr_mmap,
	.mmap_free = qedr_mmap_free,
	.modify_qp = qedr_modify_qp,
	.modify_srq = qedr_modify_srq,
	.poll_cq = qedr_poll_cq,
	.post_recv = qedr_post_recv,
	.post_send = qedr_post_send,
	.post_srq_recv = qedr_post_srq_recv,
	.process_mad = qedr_process_mad,
	.query_device = qedr_query_device,
	.query_port = qedr_query_port,
	.query_qp = qedr_query_qp,
	.query_srq = qedr_query_srq,
	.reg_user_mr = qedr_reg_user_mr,
	.req_notify_cq = qedr_arm_cq,

	INIT_RDMA_OBJ_SIZE(ib_ah, qedr_ah, ibah),
	INIT_RDMA_OBJ_SIZE(ib_cq, qedr_cq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_pd, qedr_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_qp, qedr_qp, ibqp),
	INIT_RDMA_OBJ_SIZE(ib_srq, qedr_srq, ibsrq),
	INIT_RDMA_OBJ_SIZE(ib_xrcd, qedr_xrcd, ibxrcd),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, qedr_ucontext, ibucontext),
};

static int qedr_register_device(struct qedr_dev *dev)
{
	int rc;

	dev->ibdev.node_guid = dev->attr.node_guid;
	memcpy(dev->ibdev.node_desc, QEDR_NODE_DESC, sizeof(QEDR_NODE_DESC));

	if (IS_IWARP(dev)) {
		rc = qedr_iw_register_device(dev);
		if (rc)
			return rc;
	} else {
		qedr_roce_register_device(dev);
	}

	dev->ibdev.phys_port_cnt = 1;
	dev->ibdev.num_comp_vectors = dev->num_cnq;
	dev->ibdev.dev.parent = &dev->pdev->dev;

	ib_set_device_ops(&dev->ibdev, &qedr_dev_ops);

	rc = ib_device_set_netdev(&dev->ibdev, dev->ndev, 1);
	if (rc)
		return rc;

	dma_set_max_seg_size(&dev->pdev->dev, UINT_MAX);
	return ib_register_device(&dev->ibdev, "qedr%d", &dev->pdev->dev);
}

/* This function allocates fast-path status block memory */
static int qedr_alloc_mem_sb(struct qedr_dev *dev,
			     struct qed_sb_info *sb_info, u16 sb_id)
{
	struct status_block *sb_virt;
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
		dev->ops->common->sb_release(dev->cdev, sb_info, sb_id,
					     QED_SB_TYPE_CNQ);
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
	struct qed_chain_init_params params = {
		.mode		= QED_CHAIN_MODE_PBL,
		.intended_use	= QED_CHAIN_USE_TO_CONSUME,
		.cnt_type	= QED_CHAIN_CNT_TYPE_U16,
		.elem_size	= sizeof(struct regpair *),
	};
	struct qedr_cnq *cnq;
	__le16 *cons_pi;
	int i, rc;

	dev->sgid_tbl = kcalloc(QEDR_MAX_SGID, sizeof(union ib_gid),
				GFP_KERNEL);
	if (!dev->sgid_tbl)
		return -ENOMEM;

	spin_lock_init(&dev->sgid_lock);
	xa_init_flags(&dev->srqs, XA_FLAGS_LOCK_IRQ);

	if (IS_IWARP(dev)) {
		xa_init(&dev->qps);
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
	params.num_elems = min_t(u32, QED_RDMA_MAX_CNQ_SIZE,
				 QEDR_ROCE_MAX_CNQ_SIZE);

	for (i = 0; i < dev->num_cnq; i++) {
		cnq = &dev->cnq_array[i];

		rc = qedr_alloc_mem_sb(dev, &dev->sb_array[i],
				       dev->sb_start + i);
		if (rc)
			goto err3;

		rc = dev->ops->common->chain_alloc(dev->cdev, &cnq->pbl,
						   &params);
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
	u16 idx;
	int i;

	for (i = 0; i < dev->int_info.used_cnt; i++) {
		if (dev->int_info.msix_cnt) {
			idx = i * dev->num_hwfns + dev->affin_hwfn_idx;
			vector = dev->int_info.msix[idx].vector;
			synchronize_irq(vector);
			free_irq(vector, &dev->cnq_array[i]);
		}
	}

	dev->int_info.used_cnt = 0;
}

static int qedr_req_msix_irqs(struct qedr_dev *dev)
{
	int i, rc = 0;
	u16 idx;

	if (dev->num_cnq > dev->int_info.msix_cnt) {
		DP_ERR(dev,
		       "Interrupt mismatch: %d CNQ queues > %d MSI-x vectors\n",
		       dev->num_cnq, dev->int_info.msix_cnt);
		return -EINVAL;
	}

	for (i = 0; i < dev->num_cnq; i++) {
		idx = i * dev->num_hwfns + dev->affin_hwfn_idx;
		rc = request_irq(dev->int_info.msix[idx].vector,
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
	page_size = ~qed_attr->page_size_caps + 1;
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
#define EVENT_TYPE_SRQ		3
	struct qedr_dev *dev = (struct qedr_dev *)context;
	struct regpair *async_handle = (struct regpair *)fw_handle;
	u64 roce_handle64 = ((u64) async_handle->hi << 32) + async_handle->lo;
	u8 event_type = EVENT_TYPE_NOT_DEFINED;
	struct ib_event event;
	struct ib_srq *ibsrq;
	struct qedr_srq *srq;
	unsigned long flags;
	struct ib_cq *ibcq;
	struct ib_qp *ibqp;
	struct qedr_cq *cq;
	struct qedr_qp *qp;
	u16 srq_id;

	if (IS_ROCE(dev)) {
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
		case ROCE_ASYNC_EVENT_SRQ_LIMIT:
			event.event = IB_EVENT_SRQ_LIMIT_REACHED;
			event_type = EVENT_TYPE_SRQ;
			break;
		case ROCE_ASYNC_EVENT_SRQ_EMPTY:
			event.event = IB_EVENT_SRQ_ERR;
			event_type = EVENT_TYPE_SRQ;
			break;
		case ROCE_ASYNC_EVENT_XRC_DOMAIN_ERR:
			event.event = IB_EVENT_QP_ACCESS_ERR;
			event_type = EVENT_TYPE_QP;
			break;
		case ROCE_ASYNC_EVENT_INVALID_XRCETH_ERR:
			event.event = IB_EVENT_QP_ACCESS_ERR;
			event_type = EVENT_TYPE_QP;
			break;
		case ROCE_ASYNC_EVENT_XRC_SRQ_CATASTROPHIC_ERR:
			event.event = IB_EVENT_CQ_ERR;
			event_type = EVENT_TYPE_CQ;
			break;
		default:
			DP_ERR(dev, "unsupported event %d on handle=%llx\n",
			       e_code, roce_handle64);
		}
	} else {
		switch (e_code) {
		case QED_IWARP_EVENT_SRQ_LIMIT:
			event.event = IB_EVENT_SRQ_LIMIT_REACHED;
			event_type = EVENT_TYPE_SRQ;
			break;
		case QED_IWARP_EVENT_SRQ_EMPTY:
			event.event = IB_EVENT_SRQ_ERR;
			event_type = EVENT_TYPE_SRQ;
			break;
		default:
		DP_ERR(dev, "unsupported event %d on handle=%llx\n", e_code,
		       roce_handle64);
		}
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
		DP_ERR(dev, "CQ event %d on handle %p\n", e_code, cq);
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
		DP_ERR(dev, "QP event %d on handle %p\n", e_code, qp);
		break;
	case EVENT_TYPE_SRQ:
		srq_id = (u16)roce_handle64;
		xa_lock_irqsave(&dev->srqs, flags);
		srq = xa_load(&dev->srqs, srq_id);
		if (srq) {
			ibsrq = &srq->ibsrq;
			if (ibsrq->event_handler) {
				event.device = ibsrq->device;
				event.element.srq = ibsrq;
				ibsrq->event_handler(&event,
						     ibsrq->srq_context);
			}
		} else {
			DP_NOTICE(dev,
				  "SRQ event with NULL pointer ibsrq. Handle=%llx\n",
				  roce_handle64);
		}
		xa_unlock_irqrestore(&dev->srqs, flags);
		DP_NOTICE(dev, "SRQ event %d on handle %p\n", e_code, srq);
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

	dev->db_addr = out_params.dpi_addr;
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
	int rc = 0;

	dev = ib_alloc_device(qedr_dev, ibdev);
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

	if (IS_IWARP(dev) && QEDR_IS_CMT(dev)) {
		rc = dev->ops->iwarp_set_engine_affin(cdev, false);
		if (rc) {
			DP_ERR(dev, "iWARP is disabled over a 100g device Enabling it may impact L2 performance. To enable it run devlink dev param set <dev> name iwarp_cmt value true cmode runtime\n");
			goto init_err;
		}
	}
	dev->affin_hwfn_idx = dev->ops->common->get_affin_hwfn_idx(cdev);

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

	if (!test_and_set_bit(QEDR_ENET_STATE_BIT, &dev->enet_state))
		qedr_ib_dispatch_event(dev, QEDR_PORT, IB_EVENT_PORT_ACTIVE);

	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr driver loaded successfully\n");
	return dev;

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
	ib_unregister_device(&dev->ibdev);

	qedr_stop_hw(dev);
	qedr_sync_free_irqs(dev);
	qedr_free_resources(dev);

	if (IS_IWARP(dev) && QEDR_IS_CMT(dev))
		dev->ops->iwarp_set_engine_affin(dev->cdev, true);

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
	case QEDE_CHANGE_MTU:
		if (rdma_protocol_iwarp(&dev->ibdev, 1))
			if (dev->ndev->mtu != dev->iwarp_max_mtu)
				DP_NOTICE(dev,
					  "Mtu was changed from %d to %d. This will not take affect for iWARP until qedr is reloaded\n",
					  dev->iwarp_max_mtu, dev->ndev->mtu);
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
