/*******************************************************************************
 * This file contains iSCSI extentions for RDMA (iSER) Verbs
 *
 * (c) Copyright 2013 Datera, Inc.
 *
 * Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 ****************************************************************************/

#include <linux/string.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/iscsi/iscsi_transport.h>
#include <linux/semaphore.h>

#include "isert_proto.h"
#include "ib_isert.h"

#define	ISERT_MAX_CONN		8
#define ISER_MAX_RX_CQ_LEN	(ISERT_QP_MAX_RECV_DTOS * ISERT_MAX_CONN)
#define ISER_MAX_TX_CQ_LEN	(ISERT_QP_MAX_REQ_DTOS  * ISERT_MAX_CONN)

static DEFINE_MUTEX(device_list_mutex);
static LIST_HEAD(device_list);
static struct workqueue_struct *isert_rx_wq;
static struct workqueue_struct *isert_comp_wq;
static struct workqueue_struct *isert_release_wq;

static void
isert_unmap_cmd(struct isert_cmd *isert_cmd, struct isert_conn *isert_conn);
static int
isert_map_rdma(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
	       struct isert_rdma_wr *wr);
static void
isert_unreg_rdma(struct isert_cmd *isert_cmd, struct isert_conn *isert_conn);
static int
isert_reg_rdma(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
	       struct isert_rdma_wr *wr);
static int
isert_put_response(struct iscsi_conn *conn, struct iscsi_cmd *cmd);
static int
isert_rdma_post_recvl(struct isert_conn *isert_conn);
static int
isert_rdma_accept(struct isert_conn *isert_conn);
struct rdma_cm_id *isert_setup_id(struct isert_np *isert_np);

static inline bool
isert_prot_cmd(struct isert_conn *conn, struct se_cmd *cmd)
{
	return (conn->pi_support &&
		cmd->prot_op != TARGET_PROT_NORMAL);
}


static void
isert_qp_event_callback(struct ib_event *e, void *context)
{
	struct isert_conn *isert_conn = (struct isert_conn *)context;

	pr_err("isert_qp_event_callback event: %d\n", e->event);
	switch (e->event) {
	case IB_EVENT_COMM_EST:
		rdma_notify(isert_conn->conn_cm_id, IB_EVENT_COMM_EST);
		break;
	case IB_EVENT_QP_LAST_WQE_REACHED:
		pr_warn("Reached TX IB_EVENT_QP_LAST_WQE_REACHED:\n");
		break;
	default:
		break;
	}
}

static int
isert_query_device(struct ib_device *ib_dev, struct ib_device_attr *devattr)
{
	int ret;

	ret = ib_query_device(ib_dev, devattr);
	if (ret) {
		pr_err("ib_query_device() failed: %d\n", ret);
		return ret;
	}
	pr_debug("devattr->max_sge: %d\n", devattr->max_sge);
	pr_debug("devattr->max_sge_rd: %d\n", devattr->max_sge_rd);

	return 0;
}

static int
isert_conn_setup_qp(struct isert_conn *isert_conn, struct rdma_cm_id *cma_id)
{
	struct isert_device *device = isert_conn->conn_device;
	struct ib_qp_init_attr attr;
	int ret, index, min_index = 0;

	mutex_lock(&device_list_mutex);
	for (index = 0; index < device->cqs_used; index++)
		if (device->cq_active_qps[index] <
		    device->cq_active_qps[min_index])
			min_index = index;
	device->cq_active_qps[min_index]++;
	pr_debug("isert_conn_setup_qp: Using min_index: %d\n", min_index);
	mutex_unlock(&device_list_mutex);

	memset(&attr, 0, sizeof(struct ib_qp_init_attr));
	attr.event_handler = isert_qp_event_callback;
	attr.qp_context = isert_conn;
	attr.send_cq = device->dev_tx_cq[min_index];
	attr.recv_cq = device->dev_rx_cq[min_index];
	attr.cap.max_send_wr = ISERT_QP_MAX_REQ_DTOS;
	attr.cap.max_recv_wr = ISERT_QP_MAX_RECV_DTOS;
	/*
	 * FIXME: Use devattr.max_sge - 2 for max_send_sge as
	 * work-around for RDMA_READs with ConnectX-2.
	 *
	 * Also, still make sure to have at least two SGEs for
	 * outgoing control PDU responses.
	 */
	attr.cap.max_send_sge = max(2, device->dev_attr.max_sge - 2);
	isert_conn->max_sge = attr.cap.max_send_sge;

	attr.cap.max_recv_sge = 1;
	attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	attr.qp_type = IB_QPT_RC;
	if (device->pi_capable)
		attr.create_flags |= IB_QP_CREATE_SIGNATURE_EN;

	pr_debug("isert_conn_setup_qp cma_id->device: %p\n",
		 cma_id->device);
	pr_debug("isert_conn_setup_qp conn_pd->device: %p\n",
		 isert_conn->conn_pd->device);

	ret = rdma_create_qp(cma_id, isert_conn->conn_pd, &attr);
	if (ret) {
		pr_err("rdma_create_qp failed for cma_id %d\n", ret);
		goto err;
	}
	isert_conn->conn_qp = cma_id->qp;
	pr_debug("rdma_create_qp() returned success >>>>>>>>>>>>>>>>>>>>>>>>>.\n");

	return 0;
err:
	mutex_lock(&device_list_mutex);
	device->cq_active_qps[min_index]--;
	mutex_unlock(&device_list_mutex);

	return ret;
}

static void
isert_cq_event_callback(struct ib_event *e, void *context)
{
	pr_debug("isert_cq_event_callback event: %d\n", e->event);
}

static int
isert_alloc_rx_descriptors(struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iser_rx_desc *rx_desc;
	struct ib_sge *rx_sg;
	u64 dma_addr;
	int i, j;

	isert_conn->conn_rx_descs = kzalloc(ISERT_QP_MAX_RECV_DTOS *
				sizeof(struct iser_rx_desc), GFP_KERNEL);
	if (!isert_conn->conn_rx_descs)
		goto fail;

	rx_desc = isert_conn->conn_rx_descs;

	for (i = 0; i < ISERT_QP_MAX_RECV_DTOS; i++, rx_desc++)  {
		dma_addr = ib_dma_map_single(ib_dev, (void *)rx_desc,
					ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(ib_dev, dma_addr))
			goto dma_map_fail;

		rx_desc->dma_addr = dma_addr;

		rx_sg = &rx_desc->rx_sg;
		rx_sg->addr = rx_desc->dma_addr;
		rx_sg->length = ISER_RX_PAYLOAD_SIZE;
		rx_sg->lkey = isert_conn->conn_mr->lkey;
	}

	isert_conn->conn_rx_desc_head = 0;
	return 0;

dma_map_fail:
	rx_desc = isert_conn->conn_rx_descs;
	for (j = 0; j < i; j++, rx_desc++) {
		ib_dma_unmap_single(ib_dev, rx_desc->dma_addr,
				    ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
	}
	kfree(isert_conn->conn_rx_descs);
	isert_conn->conn_rx_descs = NULL;
fail:
	return -ENOMEM;
}

static void
isert_free_rx_descriptors(struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iser_rx_desc *rx_desc;
	int i;

	if (!isert_conn->conn_rx_descs)
		return;

	rx_desc = isert_conn->conn_rx_descs;
	for (i = 0; i < ISERT_QP_MAX_RECV_DTOS; i++, rx_desc++)  {
		ib_dma_unmap_single(ib_dev, rx_desc->dma_addr,
				    ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
	}

	kfree(isert_conn->conn_rx_descs);
	isert_conn->conn_rx_descs = NULL;
}

static void isert_cq_tx_work(struct work_struct *);
static void isert_cq_tx_callback(struct ib_cq *, void *);
static void isert_cq_rx_work(struct work_struct *);
static void isert_cq_rx_callback(struct ib_cq *, void *);

static int
isert_create_device_ib_res(struct isert_device *device)
{
	struct ib_device *ib_dev = device->ib_device;
	struct isert_cq_desc *cq_desc;
	struct ib_device_attr *dev_attr;
	int ret = 0, i, j;
	int max_rx_cqe, max_tx_cqe;

	dev_attr = &device->dev_attr;
	ret = isert_query_device(ib_dev, dev_attr);
	if (ret)
		return ret;

	max_rx_cqe = min(ISER_MAX_RX_CQ_LEN, dev_attr->max_cqe);
	max_tx_cqe = min(ISER_MAX_TX_CQ_LEN, dev_attr->max_cqe);

	/* asign function handlers */
	if (dev_attr->device_cap_flags & IB_DEVICE_MEM_MGT_EXTENSIONS &&
	    dev_attr->device_cap_flags & IB_DEVICE_SIGNATURE_HANDOVER) {
		device->use_fastreg = 1;
		device->reg_rdma_mem = isert_reg_rdma;
		device->unreg_rdma_mem = isert_unreg_rdma;
	} else {
		device->use_fastreg = 0;
		device->reg_rdma_mem = isert_map_rdma;
		device->unreg_rdma_mem = isert_unmap_cmd;
	}

	/* Check signature cap */
	device->pi_capable = dev_attr->device_cap_flags &
			     IB_DEVICE_SIGNATURE_HANDOVER ? true : false;

	device->cqs_used = min_t(int, num_online_cpus(),
				 device->ib_device->num_comp_vectors);
	device->cqs_used = min(ISERT_MAX_CQ, device->cqs_used);
	pr_debug("Using %d CQs, device %s supports %d vectors support "
		 "Fast registration %d pi_capable %d\n",
		 device->cqs_used, device->ib_device->name,
		 device->ib_device->num_comp_vectors, device->use_fastreg,
		 device->pi_capable);
	device->cq_desc = kzalloc(sizeof(struct isert_cq_desc) *
				device->cqs_used, GFP_KERNEL);
	if (!device->cq_desc) {
		pr_err("Unable to allocate device->cq_desc\n");
		return -ENOMEM;
	}
	cq_desc = device->cq_desc;

	for (i = 0; i < device->cqs_used; i++) {
		cq_desc[i].device = device;
		cq_desc[i].cq_index = i;

		INIT_WORK(&cq_desc[i].cq_rx_work, isert_cq_rx_work);
		device->dev_rx_cq[i] = ib_create_cq(device->ib_device,
						isert_cq_rx_callback,
						isert_cq_event_callback,
						(void *)&cq_desc[i],
						max_rx_cqe, i);
		if (IS_ERR(device->dev_rx_cq[i])) {
			ret = PTR_ERR(device->dev_rx_cq[i]);
			device->dev_rx_cq[i] = NULL;
			goto out_cq;
		}

		INIT_WORK(&cq_desc[i].cq_tx_work, isert_cq_tx_work);
		device->dev_tx_cq[i] = ib_create_cq(device->ib_device,
						isert_cq_tx_callback,
						isert_cq_event_callback,
						(void *)&cq_desc[i],
						max_tx_cqe, i);
		if (IS_ERR(device->dev_tx_cq[i])) {
			ret = PTR_ERR(device->dev_tx_cq[i]);
			device->dev_tx_cq[i] = NULL;
			goto out_cq;
		}

		ret = ib_req_notify_cq(device->dev_rx_cq[i], IB_CQ_NEXT_COMP);
		if (ret)
			goto out_cq;

		ret = ib_req_notify_cq(device->dev_tx_cq[i], IB_CQ_NEXT_COMP);
		if (ret)
			goto out_cq;
	}

	return 0;

out_cq:
	for (j = 0; j < i; j++) {
		cq_desc = &device->cq_desc[j];

		if (device->dev_rx_cq[j]) {
			cancel_work_sync(&cq_desc->cq_rx_work);
			ib_destroy_cq(device->dev_rx_cq[j]);
		}
		if (device->dev_tx_cq[j]) {
			cancel_work_sync(&cq_desc->cq_tx_work);
			ib_destroy_cq(device->dev_tx_cq[j]);
		}
	}
	kfree(device->cq_desc);

	return ret;
}

static void
isert_free_device_ib_res(struct isert_device *device)
{
	struct isert_cq_desc *cq_desc;
	int i;

	for (i = 0; i < device->cqs_used; i++) {
		cq_desc = &device->cq_desc[i];

		cancel_work_sync(&cq_desc->cq_rx_work);
		cancel_work_sync(&cq_desc->cq_tx_work);
		ib_destroy_cq(device->dev_rx_cq[i]);
		ib_destroy_cq(device->dev_tx_cq[i]);
		device->dev_rx_cq[i] = NULL;
		device->dev_tx_cq[i] = NULL;
	}

	kfree(device->cq_desc);
}

static void
isert_device_try_release(struct isert_device *device)
{
	mutex_lock(&device_list_mutex);
	device->refcount--;
	if (!device->refcount) {
		isert_free_device_ib_res(device);
		list_del(&device->dev_node);
		kfree(device);
	}
	mutex_unlock(&device_list_mutex);
}

static struct isert_device *
isert_device_find_by_ib_dev(struct rdma_cm_id *cma_id)
{
	struct isert_device *device;
	int ret;

	mutex_lock(&device_list_mutex);
	list_for_each_entry(device, &device_list, dev_node) {
		if (device->ib_device->node_guid == cma_id->device->node_guid) {
			device->refcount++;
			mutex_unlock(&device_list_mutex);
			return device;
		}
	}

	device = kzalloc(sizeof(struct isert_device), GFP_KERNEL);
	if (!device) {
		mutex_unlock(&device_list_mutex);
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&device->dev_node);

	device->ib_device = cma_id->device;
	ret = isert_create_device_ib_res(device);
	if (ret) {
		kfree(device);
		mutex_unlock(&device_list_mutex);
		return ERR_PTR(ret);
	}

	device->refcount++;
	list_add_tail(&device->dev_node, &device_list);
	mutex_unlock(&device_list_mutex);

	return device;
}

static void
isert_conn_free_fastreg_pool(struct isert_conn *isert_conn)
{
	struct fast_reg_descriptor *fr_desc, *tmp;
	int i = 0;

	if (list_empty(&isert_conn->conn_fr_pool))
		return;

	pr_debug("Freeing conn %p fastreg pool", isert_conn);

	list_for_each_entry_safe(fr_desc, tmp,
				 &isert_conn->conn_fr_pool, list) {
		list_del(&fr_desc->list);
		ib_free_fast_reg_page_list(fr_desc->data_frpl);
		ib_dereg_mr(fr_desc->data_mr);
		if (fr_desc->pi_ctx) {
			ib_free_fast_reg_page_list(fr_desc->pi_ctx->prot_frpl);
			ib_dereg_mr(fr_desc->pi_ctx->prot_mr);
			ib_destroy_mr(fr_desc->pi_ctx->sig_mr);
			kfree(fr_desc->pi_ctx);
		}
		kfree(fr_desc);
		++i;
	}

	if (i < isert_conn->conn_fr_pool_size)
		pr_warn("Pool still has %d regions registered\n",
			isert_conn->conn_fr_pool_size - i);
}

static int
isert_create_pi_ctx(struct fast_reg_descriptor *desc,
		    struct ib_device *device,
		    struct ib_pd *pd)
{
	struct ib_mr_init_attr mr_init_attr;
	struct pi_context *pi_ctx;
	int ret;

	pi_ctx = kzalloc(sizeof(*desc->pi_ctx), GFP_KERNEL);
	if (!pi_ctx) {
		pr_err("Failed to allocate pi context\n");
		return -ENOMEM;
	}

	pi_ctx->prot_frpl = ib_alloc_fast_reg_page_list(device,
					    ISCSI_ISER_SG_TABLESIZE);
	if (IS_ERR(pi_ctx->prot_frpl)) {
		pr_err("Failed to allocate prot frpl err=%ld\n",
			  PTR_ERR(pi_ctx->prot_frpl));
		ret = PTR_ERR(pi_ctx->prot_frpl);
		goto err_pi_ctx;
	}

	pi_ctx->prot_mr = ib_alloc_fast_reg_mr(pd, ISCSI_ISER_SG_TABLESIZE);
	if (IS_ERR(pi_ctx->prot_mr)) {
		pr_err("Failed to allocate prot frmr err=%ld\n",
			  PTR_ERR(pi_ctx->prot_mr));
		ret = PTR_ERR(pi_ctx->prot_mr);
		goto err_prot_frpl;
	}
	desc->ind |= ISERT_PROT_KEY_VALID;

	memset(&mr_init_attr, 0, sizeof(mr_init_attr));
	mr_init_attr.max_reg_descriptors = 2;
	mr_init_attr.flags |= IB_MR_SIGNATURE_EN;
	pi_ctx->sig_mr = ib_create_mr(pd, &mr_init_attr);
	if (IS_ERR(pi_ctx->sig_mr)) {
		pr_err("Failed to allocate signature enabled mr err=%ld\n",
			  PTR_ERR(pi_ctx->sig_mr));
		ret = PTR_ERR(pi_ctx->sig_mr);
		goto err_prot_mr;
	}

	desc->pi_ctx = pi_ctx;
	desc->ind |= ISERT_SIG_KEY_VALID;
	desc->ind &= ~ISERT_PROTECTED;

	return 0;

err_prot_mr:
	ib_dereg_mr(desc->pi_ctx->prot_mr);
err_prot_frpl:
	ib_free_fast_reg_page_list(desc->pi_ctx->prot_frpl);
err_pi_ctx:
	kfree(desc->pi_ctx);

	return ret;
}

static int
isert_create_fr_desc(struct ib_device *ib_device, struct ib_pd *pd,
		     struct fast_reg_descriptor *fr_desc)
{
	int ret;

	fr_desc->data_frpl = ib_alloc_fast_reg_page_list(ib_device,
							 ISCSI_ISER_SG_TABLESIZE);
	if (IS_ERR(fr_desc->data_frpl)) {
		pr_err("Failed to allocate data frpl err=%ld\n",
		       PTR_ERR(fr_desc->data_frpl));
		return PTR_ERR(fr_desc->data_frpl);
	}

	fr_desc->data_mr = ib_alloc_fast_reg_mr(pd, ISCSI_ISER_SG_TABLESIZE);
	if (IS_ERR(fr_desc->data_mr)) {
		pr_err("Failed to allocate data frmr err=%ld\n",
		       PTR_ERR(fr_desc->data_mr));
		ret = PTR_ERR(fr_desc->data_mr);
		goto err_data_frpl;
	}
	fr_desc->ind |= ISERT_DATA_KEY_VALID;

	pr_debug("Created fr_desc %p\n", fr_desc);

	return 0;

err_data_frpl:
	ib_free_fast_reg_page_list(fr_desc->data_frpl);

	return ret;
}

static int
isert_conn_create_fastreg_pool(struct isert_conn *isert_conn)
{
	struct fast_reg_descriptor *fr_desc;
	struct isert_device *device = isert_conn->conn_device;
	struct se_session *se_sess = isert_conn->conn->sess->se_sess;
	struct se_node_acl *se_nacl = se_sess->se_node_acl;
	int i, ret, tag_num;
	/*
	 * Setup the number of FRMRs based upon the number of tags
	 * available to session in iscsi_target_locate_portal().
	 */
	tag_num = max_t(u32, ISCSIT_MIN_TAGS, se_nacl->queue_depth);
	tag_num = (tag_num * 2) + ISCSIT_EXTRA_TAGS;

	isert_conn->conn_fr_pool_size = 0;
	for (i = 0; i < tag_num; i++) {
		fr_desc = kzalloc(sizeof(*fr_desc), GFP_KERNEL);
		if (!fr_desc) {
			pr_err("Failed to allocate fast_reg descriptor\n");
			ret = -ENOMEM;
			goto err;
		}

		ret = isert_create_fr_desc(device->ib_device,
					   isert_conn->conn_pd, fr_desc);
		if (ret) {
			pr_err("Failed to create fastreg descriptor err=%d\n",
			       ret);
			kfree(fr_desc);
			goto err;
		}

		list_add_tail(&fr_desc->list, &isert_conn->conn_fr_pool);
		isert_conn->conn_fr_pool_size++;
	}

	pr_debug("Creating conn %p fastreg pool size=%d",
		 isert_conn, isert_conn->conn_fr_pool_size);

	return 0;

err:
	isert_conn_free_fastreg_pool(isert_conn);
	return ret;
}

static int
isert_connect_request(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	struct isert_np *isert_np = cma_id->context;
	struct iscsi_np *np = isert_np->np;
	struct isert_conn *isert_conn;
	struct isert_device *device;
	struct ib_device *ib_dev = cma_id->device;
	int ret = 0;

	spin_lock_bh(&np->np_thread_lock);
	if (!np->enabled) {
		spin_unlock_bh(&np->np_thread_lock);
		pr_debug("iscsi_np is not enabled, reject connect request\n");
		return rdma_reject(cma_id, NULL, 0);
	}
	spin_unlock_bh(&np->np_thread_lock);

	pr_debug("Entering isert_connect_request cma_id: %p, context: %p\n",
		 cma_id, cma_id->context);

	isert_conn = kzalloc(sizeof(struct isert_conn), GFP_KERNEL);
	if (!isert_conn) {
		pr_err("Unable to allocate isert_conn\n");
		return -ENOMEM;
	}
	isert_conn->state = ISER_CONN_INIT;
	INIT_LIST_HEAD(&isert_conn->conn_accept_node);
	init_completion(&isert_conn->conn_login_comp);
	init_completion(&isert_conn->login_req_comp);
	init_completion(&isert_conn->conn_wait);
	init_completion(&isert_conn->conn_wait_comp_err);
	kref_init(&isert_conn->conn_kref);
	mutex_init(&isert_conn->conn_mutex);
	spin_lock_init(&isert_conn->conn_lock);
	INIT_LIST_HEAD(&isert_conn->conn_fr_pool);

	isert_conn->conn_cm_id = cma_id;

	isert_conn->login_buf = kzalloc(ISCSI_DEF_MAX_RECV_SEG_LEN +
					ISER_RX_LOGIN_SIZE, GFP_KERNEL);
	if (!isert_conn->login_buf) {
		pr_err("Unable to allocate isert_conn->login_buf\n");
		ret = -ENOMEM;
		goto out;
	}

	isert_conn->login_req_buf = isert_conn->login_buf;
	isert_conn->login_rsp_buf = isert_conn->login_buf +
				    ISCSI_DEF_MAX_RECV_SEG_LEN;
	pr_debug("Set login_buf: %p login_req_buf: %p login_rsp_buf: %p\n",
		 isert_conn->login_buf, isert_conn->login_req_buf,
		 isert_conn->login_rsp_buf);

	isert_conn->login_req_dma = ib_dma_map_single(ib_dev,
				(void *)isert_conn->login_req_buf,
				ISCSI_DEF_MAX_RECV_SEG_LEN, DMA_FROM_DEVICE);

	ret = ib_dma_mapping_error(ib_dev, isert_conn->login_req_dma);
	if (ret) {
		pr_err("ib_dma_mapping_error failed for login_req_dma: %d\n",
		       ret);
		isert_conn->login_req_dma = 0;
		goto out_login_buf;
	}

	isert_conn->login_rsp_dma = ib_dma_map_single(ib_dev,
					(void *)isert_conn->login_rsp_buf,
					ISER_RX_LOGIN_SIZE, DMA_TO_DEVICE);

	ret = ib_dma_mapping_error(ib_dev, isert_conn->login_rsp_dma);
	if (ret) {
		pr_err("ib_dma_mapping_error failed for login_rsp_dma: %d\n",
		       ret);
		isert_conn->login_rsp_dma = 0;
		goto out_req_dma_map;
	}

	device = isert_device_find_by_ib_dev(cma_id);
	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		goto out_rsp_dma_map;
	}

	/* Set max inflight RDMA READ requests */
	isert_conn->initiator_depth = min_t(u8,
				event->param.conn.initiator_depth,
				device->dev_attr.max_qp_init_rd_atom);
	pr_debug("Using initiator_depth: %u\n", isert_conn->initiator_depth);

	isert_conn->conn_device = device;
	isert_conn->conn_pd = ib_alloc_pd(isert_conn->conn_device->ib_device);
	if (IS_ERR(isert_conn->conn_pd)) {
		ret = PTR_ERR(isert_conn->conn_pd);
		pr_err("ib_alloc_pd failed for conn %p: ret=%d\n",
		       isert_conn, ret);
		goto out_pd;
	}

	isert_conn->conn_mr = ib_get_dma_mr(isert_conn->conn_pd,
					   IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(isert_conn->conn_mr)) {
		ret = PTR_ERR(isert_conn->conn_mr);
		pr_err("ib_get_dma_mr failed for conn %p: ret=%d\n",
		       isert_conn, ret);
		goto out_mr;
	}

	ret = isert_conn_setup_qp(isert_conn, cma_id);
	if (ret)
		goto out_conn_dev;

	ret = isert_rdma_post_recvl(isert_conn);
	if (ret)
		goto out_conn_dev;

	ret = isert_rdma_accept(isert_conn);
	if (ret)
		goto out_conn_dev;

	mutex_lock(&isert_np->np_accept_mutex);
	list_add_tail(&isert_conn->conn_accept_node, &isert_np->np_accept_list);
	mutex_unlock(&isert_np->np_accept_mutex);

	pr_debug("isert_connect_request() up np_sem np: %p\n", np);
	up(&isert_np->np_sem);
	return 0;

out_conn_dev:
	ib_dereg_mr(isert_conn->conn_mr);
out_mr:
	ib_dealloc_pd(isert_conn->conn_pd);
out_pd:
	isert_device_try_release(device);
out_rsp_dma_map:
	ib_dma_unmap_single(ib_dev, isert_conn->login_rsp_dma,
			    ISER_RX_LOGIN_SIZE, DMA_TO_DEVICE);
out_req_dma_map:
	ib_dma_unmap_single(ib_dev, isert_conn->login_req_dma,
			    ISCSI_DEF_MAX_RECV_SEG_LEN, DMA_FROM_DEVICE);
out_login_buf:
	kfree(isert_conn->login_buf);
out:
	kfree(isert_conn);
	rdma_reject(cma_id, NULL, 0);
	return ret;
}

static void
isert_connect_release(struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct isert_device *device = isert_conn->conn_device;
	int cq_index;

	pr_debug("Entering isert_connect_release(): >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

	if (device && device->use_fastreg)
		isert_conn_free_fastreg_pool(isert_conn);

	isert_free_rx_descriptors(isert_conn);
	rdma_destroy_id(isert_conn->conn_cm_id);

	if (isert_conn->conn_qp) {
		cq_index = ((struct isert_cq_desc *)
			isert_conn->conn_qp->recv_cq->cq_context)->cq_index;
		pr_debug("isert_connect_release: cq_index: %d\n", cq_index);
		mutex_lock(&device_list_mutex);
		isert_conn->conn_device->cq_active_qps[cq_index]--;
		mutex_unlock(&device_list_mutex);

		ib_destroy_qp(isert_conn->conn_qp);
	}

	ib_dereg_mr(isert_conn->conn_mr);
	ib_dealloc_pd(isert_conn->conn_pd);

	if (isert_conn->login_buf) {
		ib_dma_unmap_single(ib_dev, isert_conn->login_rsp_dma,
				    ISER_RX_LOGIN_SIZE, DMA_TO_DEVICE);
		ib_dma_unmap_single(ib_dev, isert_conn->login_req_dma,
				    ISCSI_DEF_MAX_RECV_SEG_LEN,
				    DMA_FROM_DEVICE);
		kfree(isert_conn->login_buf);
	}
	kfree(isert_conn);

	if (device)
		isert_device_try_release(device);

	pr_debug("Leaving isert_connect_release >>>>>>>>>>>>\n");
}

static void
isert_connected_handler(struct rdma_cm_id *cma_id)
{
	struct isert_conn *isert_conn = cma_id->qp->qp_context;

	pr_info("conn %p\n", isert_conn);

	if (!kref_get_unless_zero(&isert_conn->conn_kref)) {
		pr_warn("conn %p connect_release is running\n", isert_conn);
		return;
	}

	mutex_lock(&isert_conn->conn_mutex);
	if (isert_conn->state != ISER_CONN_FULL_FEATURE)
		isert_conn->state = ISER_CONN_UP;
	mutex_unlock(&isert_conn->conn_mutex);
}

static void
isert_release_conn_kref(struct kref *kref)
{
	struct isert_conn *isert_conn = container_of(kref,
				struct isert_conn, conn_kref);

	pr_debug("Calling isert_connect_release for final kref %s/%d\n",
		 current->comm, current->pid);

	isert_connect_release(isert_conn);
}

static void
isert_put_conn(struct isert_conn *isert_conn)
{
	kref_put(&isert_conn->conn_kref, isert_release_conn_kref);
}

/**
 * isert_conn_terminate() - Initiate connection termination
 * @isert_conn: isert connection struct
 *
 * Notes:
 * In case the connection state is FULL_FEATURE, move state
 * to TEMINATING and start teardown sequence (rdma_disconnect).
 * In case the connection state is UP, complete flush as well.
 *
 * This routine must be called with conn_mutex held. Thus it is
 * safe to call multiple times.
 */
static void
isert_conn_terminate(struct isert_conn *isert_conn)
{
	int err;

	switch (isert_conn->state) {
	case ISER_CONN_TERMINATING:
		break;
	case ISER_CONN_UP:
		/*
		 * No flush completions will occur as we didn't
		 * get to ISER_CONN_FULL_FEATURE yet, complete
		 * to allow teardown progress.
		 */
		complete(&isert_conn->conn_wait_comp_err);
	case ISER_CONN_FULL_FEATURE: /* FALLTHRU */
		pr_info("Terminating conn %p state %d\n",
			   isert_conn, isert_conn->state);
		isert_conn->state = ISER_CONN_TERMINATING;
		err = rdma_disconnect(isert_conn->conn_cm_id);
		if (err)
			pr_warn("Failed rdma_disconnect isert_conn %p\n",
				   isert_conn);
		break;
	default:
		pr_warn("conn %p teminating in state %d\n",
			   isert_conn, isert_conn->state);
	}
}

static int
isert_np_cma_handler(struct isert_np *isert_np,
		     enum rdma_cm_event_type event)
{
	pr_debug("isert np %p, handling event %d\n", isert_np, event);

	switch (event) {
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		isert_np->np_cm_id = NULL;
		break;
	case RDMA_CM_EVENT_ADDR_CHANGE:
		isert_np->np_cm_id = isert_setup_id(isert_np);
		if (IS_ERR(isert_np->np_cm_id)) {
			pr_err("isert np %p setup id failed: %ld\n",
				 isert_np, PTR_ERR(isert_np->np_cm_id));
			isert_np->np_cm_id = NULL;
		}
		break;
	default:
		pr_err("isert np %p Unexpected event %d\n",
			  isert_np, event);
	}

	return -1;
}

static int
isert_disconnected_handler(struct rdma_cm_id *cma_id,
			   enum rdma_cm_event_type event)
{
	struct isert_np *isert_np = cma_id->context;
	struct isert_conn *isert_conn;

	if (isert_np->np_cm_id == cma_id)
		return isert_np_cma_handler(cma_id->context, event);

	isert_conn = cma_id->qp->qp_context;

	mutex_lock(&isert_conn->conn_mutex);
	isert_conn_terminate(isert_conn);
	mutex_unlock(&isert_conn->conn_mutex);

	pr_info("conn %p completing conn_wait\n", isert_conn);
	complete(&isert_conn->conn_wait);

	return 0;
}

static void
isert_connect_error(struct rdma_cm_id *cma_id)
{
	struct isert_conn *isert_conn = cma_id->qp->qp_context;

	isert_put_conn(isert_conn);
}

static int
isert_cma_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	int ret = 0;

	pr_debug("isert_cma_handler: event %d status %d conn %p id %p\n",
		 event->event, event->status, cma_id->context, cma_id);

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		ret = isert_connect_request(cma_id, event);
		if (ret)
			pr_err("isert_cma_handler failed RDMA_CM_EVENT: 0x%08x %d\n",
				event->event, ret);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		isert_connected_handler(cma_id);
		break;
	case RDMA_CM_EVENT_ADDR_CHANGE:    /* FALLTHRU */
	case RDMA_CM_EVENT_DISCONNECTED:   /* FALLTHRU */
	case RDMA_CM_EVENT_DEVICE_REMOVAL: /* FALLTHRU */
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:  /* FALLTHRU */
		ret = isert_disconnected_handler(cma_id, event->event);
		break;
	case RDMA_CM_EVENT_REJECTED:       /* FALLTHRU */
	case RDMA_CM_EVENT_UNREACHABLE:    /* FALLTHRU */
	case RDMA_CM_EVENT_CONNECT_ERROR:
		isert_connect_error(cma_id);
		break;
	default:
		pr_err("Unhandled RDMA CMA event: %d\n", event->event);
		break;
	}

	return ret;
}

static int
isert_post_recv(struct isert_conn *isert_conn, u32 count)
{
	struct ib_recv_wr *rx_wr, *rx_wr_failed;
	int i, ret;
	unsigned int rx_head = isert_conn->conn_rx_desc_head;
	struct iser_rx_desc *rx_desc;

	for (rx_wr = isert_conn->conn_rx_wr, i = 0; i < count; i++, rx_wr++) {
		rx_desc		= &isert_conn->conn_rx_descs[rx_head];
		rx_wr->wr_id	= (uintptr_t)rx_desc;
		rx_wr->sg_list	= &rx_desc->rx_sg;
		rx_wr->num_sge	= 1;
		rx_wr->next	= rx_wr + 1;
		rx_head = (rx_head + 1) & (ISERT_QP_MAX_RECV_DTOS - 1);
	}

	rx_wr--;
	rx_wr->next = NULL; /* mark end of work requests list */

	isert_conn->post_recv_buf_count += count;
	ret = ib_post_recv(isert_conn->conn_qp, isert_conn->conn_rx_wr,
				&rx_wr_failed);
	if (ret) {
		pr_err("ib_post_recv() failed with ret: %d\n", ret);
		isert_conn->post_recv_buf_count -= count;
	} else {
		pr_debug("isert_post_recv(): Posted %d RX buffers\n", count);
		isert_conn->conn_rx_desc_head = rx_head;
	}
	return ret;
}

static int
isert_post_send(struct isert_conn *isert_conn, struct iser_tx_desc *tx_desc)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct ib_send_wr send_wr, *send_wr_failed;
	int ret;

	ib_dma_sync_single_for_device(ib_dev, tx_desc->dma_addr,
				      ISER_HEADERS_LEN, DMA_TO_DEVICE);

	send_wr.next	= NULL;
	send_wr.wr_id	= (uintptr_t)tx_desc;
	send_wr.sg_list	= tx_desc->tx_sg;
	send_wr.num_sge	= tx_desc->num_sge;
	send_wr.opcode	= IB_WR_SEND;
	send_wr.send_flags = IB_SEND_SIGNALED;

	atomic_inc(&isert_conn->post_send_buf_count);

	ret = ib_post_send(isert_conn->conn_qp, &send_wr, &send_wr_failed);
	if (ret) {
		pr_err("ib_post_send() failed, ret: %d\n", ret);
		atomic_dec(&isert_conn->post_send_buf_count);
	}

	return ret;
}

static void
isert_create_send_desc(struct isert_conn *isert_conn,
		       struct isert_cmd *isert_cmd,
		       struct iser_tx_desc *tx_desc)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;

	ib_dma_sync_single_for_cpu(ib_dev, tx_desc->dma_addr,
				   ISER_HEADERS_LEN, DMA_TO_DEVICE);

	memset(&tx_desc->iser_header, 0, sizeof(struct iser_hdr));
	tx_desc->iser_header.flags = ISER_VER;

	tx_desc->num_sge = 1;
	tx_desc->isert_cmd = isert_cmd;

	if (tx_desc->tx_sg[0].lkey != isert_conn->conn_mr->lkey) {
		tx_desc->tx_sg[0].lkey = isert_conn->conn_mr->lkey;
		pr_debug("tx_desc %p lkey mismatch, fixing\n", tx_desc);
	}
}

static int
isert_init_tx_hdrs(struct isert_conn *isert_conn,
		   struct iser_tx_desc *tx_desc)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	u64 dma_addr;

	dma_addr = ib_dma_map_single(ib_dev, (void *)tx_desc,
			ISER_HEADERS_LEN, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(ib_dev, dma_addr)) {
		pr_err("ib_dma_mapping_error() failed\n");
		return -ENOMEM;
	}

	tx_desc->dma_addr = dma_addr;
	tx_desc->tx_sg[0].addr	= tx_desc->dma_addr;
	tx_desc->tx_sg[0].length = ISER_HEADERS_LEN;
	tx_desc->tx_sg[0].lkey = isert_conn->conn_mr->lkey;

	pr_debug("isert_init_tx_hdrs: Setup tx_sg[0].addr: 0x%llx length: %u"
		 " lkey: 0x%08x\n", tx_desc->tx_sg[0].addr,
		 tx_desc->tx_sg[0].length, tx_desc->tx_sg[0].lkey);

	return 0;
}

static void
isert_init_send_wr(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd,
		   struct ib_send_wr *send_wr)
{
	struct iser_tx_desc *tx_desc = &isert_cmd->tx_desc;

	isert_cmd->rdma_wr.iser_ib_op = ISER_IB_SEND;
	send_wr->wr_id = (uintptr_t)&isert_cmd->tx_desc;
	send_wr->opcode = IB_WR_SEND;
	send_wr->sg_list = &tx_desc->tx_sg[0];
	send_wr->num_sge = isert_cmd->tx_desc.num_sge;
	send_wr->send_flags = IB_SEND_SIGNALED;
}

static int
isert_rdma_post_recvl(struct isert_conn *isert_conn)
{
	struct ib_recv_wr rx_wr, *rx_wr_fail;
	struct ib_sge sge;
	int ret;

	memset(&sge, 0, sizeof(struct ib_sge));
	sge.addr = isert_conn->login_req_dma;
	sge.length = ISER_RX_LOGIN_SIZE;
	sge.lkey = isert_conn->conn_mr->lkey;

	pr_debug("Setup sge: addr: %llx length: %d 0x%08x\n",
		sge.addr, sge.length, sge.lkey);

	memset(&rx_wr, 0, sizeof(struct ib_recv_wr));
	rx_wr.wr_id = (uintptr_t)isert_conn->login_req_buf;
	rx_wr.sg_list = &sge;
	rx_wr.num_sge = 1;

	isert_conn->post_recv_buf_count++;
	ret = ib_post_recv(isert_conn->conn_qp, &rx_wr, &rx_wr_fail);
	if (ret) {
		pr_err("ib_post_recv() failed: %d\n", ret);
		isert_conn->post_recv_buf_count--;
	}

	pr_debug("ib_post_recv(): returned success >>>>>>>>>>>>>>>>>>>>>>>>\n");
	return ret;
}

static int
isert_put_login_tx(struct iscsi_conn *conn, struct iscsi_login *login,
		   u32 length)
{
	struct isert_conn *isert_conn = conn->context;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iser_tx_desc *tx_desc = &isert_conn->conn_login_tx_desc;
	int ret;

	isert_create_send_desc(isert_conn, NULL, tx_desc);

	memcpy(&tx_desc->iscsi_header, &login->rsp[0],
	       sizeof(struct iscsi_hdr));

	isert_init_tx_hdrs(isert_conn, tx_desc);

	if (length > 0) {
		struct ib_sge *tx_dsg = &tx_desc->tx_sg[1];

		ib_dma_sync_single_for_cpu(ib_dev, isert_conn->login_rsp_dma,
					   length, DMA_TO_DEVICE);

		memcpy(isert_conn->login_rsp_buf, login->rsp_buf, length);

		ib_dma_sync_single_for_device(ib_dev, isert_conn->login_rsp_dma,
					      length, DMA_TO_DEVICE);

		tx_dsg->addr	= isert_conn->login_rsp_dma;
		tx_dsg->length	= length;
		tx_dsg->lkey	= isert_conn->conn_mr->lkey;
		tx_desc->num_sge = 2;
	}
	if (!login->login_failed) {
		if (login->login_complete) {
			if (!conn->sess->sess_ops->SessionType &&
			    isert_conn->conn_device->use_fastreg) {
				ret = isert_conn_create_fastreg_pool(isert_conn);
				if (ret) {
					pr_err("Conn: %p failed to create"
					       " fastreg pool\n", isert_conn);
					return ret;
				}
			}

			ret = isert_alloc_rx_descriptors(isert_conn);
			if (ret)
				return ret;

			ret = isert_post_recv(isert_conn, ISERT_MIN_POSTED_RX);
			if (ret)
				return ret;

			/* Now we are in FULL_FEATURE phase */
			mutex_lock(&isert_conn->conn_mutex);
			isert_conn->state = ISER_CONN_FULL_FEATURE;
			mutex_unlock(&isert_conn->conn_mutex);
			goto post_send;
		}

		ret = isert_rdma_post_recvl(isert_conn);
		if (ret)
			return ret;
	}
post_send:
	ret = isert_post_send(isert_conn, tx_desc);
	if (ret)
		return ret;

	return 0;
}

static void
isert_rx_login_req(struct isert_conn *isert_conn)
{
	struct iser_rx_desc *rx_desc = (void *)isert_conn->login_req_buf;
	int rx_buflen = isert_conn->login_req_len;
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_login *login = conn->conn_login;
	int size;

	pr_info("conn %p\n", isert_conn);

	WARN_ON_ONCE(!login);

	if (login->first_request) {
		struct iscsi_login_req *login_req =
			(struct iscsi_login_req *)&rx_desc->iscsi_header;
		/*
		 * Setup the initial iscsi_login values from the leading
		 * login request PDU.
		 */
		login->leading_connection = (!login_req->tsih) ? 1 : 0;
		login->current_stage =
			(login_req->flags & ISCSI_FLAG_LOGIN_CURRENT_STAGE_MASK)
			 >> 2;
		login->version_min	= login_req->min_version;
		login->version_max	= login_req->max_version;
		memcpy(login->isid, login_req->isid, 6);
		login->cmd_sn		= be32_to_cpu(login_req->cmdsn);
		login->init_task_tag	= login_req->itt;
		login->initial_exp_statsn = be32_to_cpu(login_req->exp_statsn);
		login->cid		= be16_to_cpu(login_req->cid);
		login->tsih		= be16_to_cpu(login_req->tsih);
	}

	memcpy(&login->req[0], (void *)&rx_desc->iscsi_header, ISCSI_HDR_LEN);

	size = min(rx_buflen, MAX_KEY_VALUE_PAIRS);
	pr_debug("Using login payload size: %d, rx_buflen: %d MAX_KEY_VALUE_PAIRS: %d\n",
		 size, rx_buflen, MAX_KEY_VALUE_PAIRS);
	memcpy(login->req_buf, &rx_desc->data[0], size);

	if (login->first_request) {
		complete(&isert_conn->conn_login_comp);
		return;
	}
	schedule_delayed_work(&conn->login_work, 0);
}

static struct iscsi_cmd
*isert_allocate_cmd(struct iscsi_conn *conn)
{
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct isert_cmd *isert_cmd;
	struct iscsi_cmd *cmd;

	cmd = iscsit_allocate_cmd(conn, TASK_INTERRUPTIBLE);
	if (!cmd) {
		pr_err("Unable to allocate iscsi_cmd + isert_cmd\n");
		return NULL;
	}
	isert_cmd = iscsit_priv_cmd(cmd);
	isert_cmd->conn = isert_conn;
	isert_cmd->iscsi_cmd = cmd;

	return cmd;
}

static int
isert_handle_scsi_cmd(struct isert_conn *isert_conn,
		      struct isert_cmd *isert_cmd, struct iscsi_cmd *cmd,
		      struct iser_rx_desc *rx_desc, unsigned char *buf)
{
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_scsi_req *hdr = (struct iscsi_scsi_req *)buf;
	struct scatterlist *sg;
	int imm_data, imm_data_len, unsol_data, sg_nents, rc;
	bool dump_payload = false;

	rc = iscsit_setup_scsi_cmd(conn, cmd, buf);
	if (rc < 0)
		return rc;

	imm_data = cmd->immediate_data;
	imm_data_len = cmd->first_burst_len;
	unsol_data = cmd->unsolicited_data;

	rc = iscsit_process_scsi_cmd(conn, cmd, hdr);
	if (rc < 0) {
		return 0;
	} else if (rc > 0) {
		dump_payload = true;
		goto sequence_cmd;
	}

	if (!imm_data)
		return 0;

	sg = &cmd->se_cmd.t_data_sg[0];
	sg_nents = max(1UL, DIV_ROUND_UP(imm_data_len, PAGE_SIZE));

	pr_debug("Copying Immediate SG: %p sg_nents: %u from %p imm_data_len: %d\n",
		 sg, sg_nents, &rx_desc->data[0], imm_data_len);

	sg_copy_from_buffer(sg, sg_nents, &rx_desc->data[0], imm_data_len);

	cmd->write_data_done += imm_data_len;

	if (cmd->write_data_done == cmd->se_cmd.data_length) {
		spin_lock_bh(&cmd->istate_lock);
		cmd->cmd_flags |= ICF_GOT_LAST_DATAOUT;
		cmd->i_state = ISTATE_RECEIVED_LAST_DATAOUT;
		spin_unlock_bh(&cmd->istate_lock);
	}

sequence_cmd:
	rc = iscsit_sequence_cmd(conn, cmd, buf, hdr->cmdsn);

	if (!rc && dump_payload == false && unsol_data)
		iscsit_set_unsoliticed_dataout(cmd);
	else if (dump_payload && imm_data)
		target_put_sess_cmd(conn->sess->se_sess, &cmd->se_cmd);

	return 0;
}

static int
isert_handle_iscsi_dataout(struct isert_conn *isert_conn,
			   struct iser_rx_desc *rx_desc, unsigned char *buf)
{
	struct scatterlist *sg_start;
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_cmd *cmd = NULL;
	struct iscsi_data *hdr = (struct iscsi_data *)buf;
	u32 unsol_data_len = ntoh24(hdr->dlength);
	int rc, sg_nents, sg_off, page_off;

	rc = iscsit_check_dataout_hdr(conn, buf, &cmd);
	if (rc < 0)
		return rc;
	else if (!cmd)
		return 0;
	/*
	 * FIXME: Unexpected unsolicited_data out
	 */
	if (!cmd->unsolicited_data) {
		pr_err("Received unexpected solicited data payload\n");
		dump_stack();
		return -1;
	}

	pr_debug("Unsolicited DataOut unsol_data_len: %u, write_data_done: %u, data_length: %u\n",
		 unsol_data_len, cmd->write_data_done, cmd->se_cmd.data_length);

	sg_off = cmd->write_data_done / PAGE_SIZE;
	sg_start = &cmd->se_cmd.t_data_sg[sg_off];
	sg_nents = max(1UL, DIV_ROUND_UP(unsol_data_len, PAGE_SIZE));
	page_off = cmd->write_data_done % PAGE_SIZE;
	/*
	 * FIXME: Non page-aligned unsolicited_data out
	 */
	if (page_off) {
		pr_err("Received unexpected non-page aligned data payload\n");
		dump_stack();
		return -1;
	}
	pr_debug("Copying DataOut: sg_start: %p, sg_off: %u sg_nents: %u from %p %u\n",
		 sg_start, sg_off, sg_nents, &rx_desc->data[0], unsol_data_len);

	sg_copy_from_buffer(sg_start, sg_nents, &rx_desc->data[0],
			    unsol_data_len);

	rc = iscsit_check_dataout_payload(cmd, hdr, false);
	if (rc < 0)
		return rc;

	return 0;
}

static int
isert_handle_nop_out(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd,
		     struct iscsi_cmd *cmd, struct iser_rx_desc *rx_desc,
		     unsigned char *buf)
{
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_nopout *hdr = (struct iscsi_nopout *)buf;
	int rc;

	rc = iscsit_setup_nop_out(conn, cmd, hdr);
	if (rc < 0)
		return rc;
	/*
	 * FIXME: Add support for NOPOUT payload using unsolicited RDMA payload
	 */

	return iscsit_process_nop_out(conn, cmd, hdr);
}

static int
isert_handle_text_cmd(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd,
		      struct iscsi_cmd *cmd, struct iser_rx_desc *rx_desc,
		      struct iscsi_text *hdr)
{
	struct iscsi_conn *conn = isert_conn->conn;
	u32 payload_length = ntoh24(hdr->dlength);
	int rc;
	unsigned char *text_in;

	rc = iscsit_setup_text_cmd(conn, cmd, hdr);
	if (rc < 0)
		return rc;

	text_in = kzalloc(payload_length, GFP_KERNEL);
	if (!text_in) {
		pr_err("Unable to allocate text_in of payload_length: %u\n",
		       payload_length);
		return -ENOMEM;
	}
	cmd->text_in_ptr = text_in;

	memcpy(cmd->text_in_ptr, &rx_desc->data[0], payload_length);

	return iscsit_process_text_cmd(conn, cmd, hdr);
}

static int
isert_rx_opcode(struct isert_conn *isert_conn, struct iser_rx_desc *rx_desc,
		uint32_t read_stag, uint64_t read_va,
		uint32_t write_stag, uint64_t write_va)
{
	struct iscsi_hdr *hdr = &rx_desc->iscsi_header;
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_session *sess = conn->sess;
	struct iscsi_cmd *cmd;
	struct isert_cmd *isert_cmd;
	int ret = -EINVAL;
	u8 opcode = (hdr->opcode & ISCSI_OPCODE_MASK);

	if (sess->sess_ops->SessionType &&
	   (!(opcode & ISCSI_OP_TEXT) || !(opcode & ISCSI_OP_LOGOUT))) {
		pr_err("Got illegal opcode: 0x%02x in SessionType=Discovery,"
		       " ignoring\n", opcode);
		return 0;
	}

	switch (opcode) {
	case ISCSI_OP_SCSI_CMD:
		cmd = isert_allocate_cmd(conn);
		if (!cmd)
			break;

		isert_cmd = iscsit_priv_cmd(cmd);
		isert_cmd->read_stag = read_stag;
		isert_cmd->read_va = read_va;
		isert_cmd->write_stag = write_stag;
		isert_cmd->write_va = write_va;

		ret = isert_handle_scsi_cmd(isert_conn, isert_cmd, cmd,
					rx_desc, (unsigned char *)hdr);
		break;
	case ISCSI_OP_NOOP_OUT:
		cmd = isert_allocate_cmd(conn);
		if (!cmd)
			break;

		isert_cmd = iscsit_priv_cmd(cmd);
		ret = isert_handle_nop_out(isert_conn, isert_cmd, cmd,
					   rx_desc, (unsigned char *)hdr);
		break;
	case ISCSI_OP_SCSI_DATA_OUT:
		ret = isert_handle_iscsi_dataout(isert_conn, rx_desc,
						(unsigned char *)hdr);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		cmd = isert_allocate_cmd(conn);
		if (!cmd)
			break;

		ret = iscsit_handle_task_mgt_cmd(conn, cmd,
						(unsigned char *)hdr);
		break;
	case ISCSI_OP_LOGOUT:
		cmd = isert_allocate_cmd(conn);
		if (!cmd)
			break;

		ret = iscsit_handle_logout_cmd(conn, cmd, (unsigned char *)hdr);
		if (ret > 0)
			wait_for_completion_timeout(&conn->conn_logout_comp,
						    SECONDS_FOR_LOGOUT_COMP *
						    HZ);
		break;
	case ISCSI_OP_TEXT:
		cmd = isert_allocate_cmd(conn);
		if (!cmd)
			break;

		isert_cmd = iscsit_priv_cmd(cmd);
		ret = isert_handle_text_cmd(isert_conn, isert_cmd, cmd,
					    rx_desc, (struct iscsi_text *)hdr);
		break;
	default:
		pr_err("Got unknown iSCSI OpCode: 0x%02x\n", opcode);
		dump_stack();
		break;
	}

	return ret;
}

static void
isert_rx_do_work(struct iser_rx_desc *rx_desc, struct isert_conn *isert_conn)
{
	struct iser_hdr *iser_hdr = &rx_desc->iser_header;
	uint64_t read_va = 0, write_va = 0;
	uint32_t read_stag = 0, write_stag = 0;
	int rc;

	switch (iser_hdr->flags & 0xF0) {
	case ISCSI_CTRL:
		if (iser_hdr->flags & ISER_RSV) {
			read_stag = be32_to_cpu(iser_hdr->read_stag);
			read_va = be64_to_cpu(iser_hdr->read_va);
			pr_debug("ISER_RSV: read_stag: 0x%08x read_va: 0x%16llx\n",
				 read_stag, (unsigned long long)read_va);
		}
		if (iser_hdr->flags & ISER_WSV) {
			write_stag = be32_to_cpu(iser_hdr->write_stag);
			write_va = be64_to_cpu(iser_hdr->write_va);
			pr_debug("ISER_WSV: write__stag: 0x%08x write_va: 0x%16llx\n",
				 write_stag, (unsigned long long)write_va);
		}

		pr_debug("ISER ISCSI_CTRL PDU\n");
		break;
	case ISER_HELLO:
		pr_err("iSER Hello message\n");
		break;
	default:
		pr_warn("Unknown iSER hdr flags: 0x%02x\n", iser_hdr->flags);
		break;
	}

	rc = isert_rx_opcode(isert_conn, rx_desc,
			     read_stag, read_va, write_stag, write_va);
}

static void
isert_rx_completion(struct iser_rx_desc *desc, struct isert_conn *isert_conn,
		    unsigned long xfer_len)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iscsi_hdr *hdr;
	u64 rx_dma;
	int rx_buflen, outstanding;

	if ((char *)desc == isert_conn->login_req_buf) {
		rx_dma = isert_conn->login_req_dma;
		rx_buflen = ISER_RX_LOGIN_SIZE;
		pr_debug("ISER login_buf: Using rx_dma: 0x%llx, rx_buflen: %d\n",
			 rx_dma, rx_buflen);
	} else {
		rx_dma = desc->dma_addr;
		rx_buflen = ISER_RX_PAYLOAD_SIZE;
		pr_debug("ISER req_buf: Using rx_dma: 0x%llx, rx_buflen: %d\n",
			 rx_dma, rx_buflen);
	}

	ib_dma_sync_single_for_cpu(ib_dev, rx_dma, rx_buflen, DMA_FROM_DEVICE);

	hdr = &desc->iscsi_header;
	pr_debug("iSCSI opcode: 0x%02x, ITT: 0x%08x, flags: 0x%02x dlen: %d\n",
		 hdr->opcode, hdr->itt, hdr->flags,
		 (int)(xfer_len - ISER_HEADERS_LEN));

	if ((char *)desc == isert_conn->login_req_buf) {
		isert_conn->login_req_len = xfer_len - ISER_HEADERS_LEN;
		if (isert_conn->conn) {
			struct iscsi_login *login = isert_conn->conn->conn_login;

			if (login && !login->first_request)
				isert_rx_login_req(isert_conn);
		}
		mutex_lock(&isert_conn->conn_mutex);
		complete(&isert_conn->login_req_comp);
		mutex_unlock(&isert_conn->conn_mutex);
	} else {
		isert_rx_do_work(desc, isert_conn);
	}

	ib_dma_sync_single_for_device(ib_dev, rx_dma, rx_buflen,
				      DMA_FROM_DEVICE);

	isert_conn->post_recv_buf_count--;
	pr_debug("iSERT: Decremented post_recv_buf_count: %d\n",
		 isert_conn->post_recv_buf_count);

	if ((char *)desc == isert_conn->login_req_buf)
		return;

	outstanding = isert_conn->post_recv_buf_count;
	if (outstanding + ISERT_MIN_POSTED_RX <= ISERT_QP_MAX_RECV_DTOS) {
		int err, count = min(ISERT_QP_MAX_RECV_DTOS - outstanding,
				ISERT_MIN_POSTED_RX);
		err = isert_post_recv(isert_conn, count);
		if (err) {
			pr_err("isert_post_recv() count: %d failed, %d\n",
			       count, err);
		}
	}
}

static int
isert_map_data_buf(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd,
		   struct scatterlist *sg, u32 nents, u32 length, u32 offset,
		   enum iser_ib_op_code op, struct isert_data_buf *data)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;

	data->dma_dir = op == ISER_IB_RDMA_WRITE ?
			      DMA_TO_DEVICE : DMA_FROM_DEVICE;

	data->len = length - offset;
	data->offset = offset;
	data->sg_off = data->offset / PAGE_SIZE;

	data->sg = &sg[data->sg_off];
	data->nents = min_t(unsigned int, nents - data->sg_off,
					  ISCSI_ISER_SG_TABLESIZE);
	data->len = min_t(unsigned int, data->len, ISCSI_ISER_SG_TABLESIZE *
					PAGE_SIZE);

	data->dma_nents = ib_dma_map_sg(ib_dev, data->sg, data->nents,
					data->dma_dir);
	if (unlikely(!data->dma_nents)) {
		pr_err("Cmd: unable to dma map SGs %p\n", sg);
		return -EINVAL;
	}

	pr_debug("Mapped cmd: %p count: %u sg: %p sg_nents: %u rdma_len %d\n",
		 isert_cmd, data->dma_nents, data->sg, data->nents, data->len);

	return 0;
}

static void
isert_unmap_data_buf(struct isert_conn *isert_conn, struct isert_data_buf *data)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;

	ib_dma_unmap_sg(ib_dev, data->sg, data->nents, data->dma_dir);
	memset(data, 0, sizeof(*data));
}



static void
isert_unmap_cmd(struct isert_cmd *isert_cmd, struct isert_conn *isert_conn)
{
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;

	pr_debug("isert_unmap_cmd: %p\n", isert_cmd);

	if (wr->data.sg) {
		pr_debug("isert_unmap_cmd: %p unmap_sg op\n", isert_cmd);
		isert_unmap_data_buf(isert_conn, &wr->data);
	}

	if (wr->send_wr) {
		pr_debug("isert_unmap_cmd: %p free send_wr\n", isert_cmd);
		kfree(wr->send_wr);
		wr->send_wr = NULL;
	}

	if (wr->ib_sge) {
		pr_debug("isert_unmap_cmd: %p free ib_sge\n", isert_cmd);
		kfree(wr->ib_sge);
		wr->ib_sge = NULL;
	}
}

static void
isert_unreg_rdma(struct isert_cmd *isert_cmd, struct isert_conn *isert_conn)
{
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	LIST_HEAD(unmap_list);

	pr_debug("unreg_fastreg_cmd: %p\n", isert_cmd);

	if (wr->fr_desc) {
		pr_debug("unreg_fastreg_cmd: %p free fr_desc %p\n",
			 isert_cmd, wr->fr_desc);
		if (wr->fr_desc->ind & ISERT_PROTECTED) {
			isert_unmap_data_buf(isert_conn, &wr->prot);
			wr->fr_desc->ind &= ~ISERT_PROTECTED;
		}
		spin_lock_bh(&isert_conn->conn_lock);
		list_add_tail(&wr->fr_desc->list, &isert_conn->conn_fr_pool);
		spin_unlock_bh(&isert_conn->conn_lock);
		wr->fr_desc = NULL;
	}

	if (wr->data.sg) {
		pr_debug("unreg_fastreg_cmd: %p unmap_sg op\n", isert_cmd);
		isert_unmap_data_buf(isert_conn, &wr->data);
	}

	wr->ib_sge = NULL;
	wr->send_wr = NULL;
}

static void
isert_put_cmd(struct isert_cmd *isert_cmd, bool comp_err)
{
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;
	struct isert_conn *isert_conn = isert_cmd->conn;
	struct iscsi_conn *conn = isert_conn->conn;
	struct isert_device *device = isert_conn->conn_device;

	pr_debug("Entering isert_put_cmd: %p\n", isert_cmd);

	switch (cmd->iscsi_opcode) {
	case ISCSI_OP_SCSI_CMD:
		spin_lock_bh(&conn->cmd_lock);
		if (!list_empty(&cmd->i_conn_node))
			list_del_init(&cmd->i_conn_node);
		spin_unlock_bh(&conn->cmd_lock);

		if (cmd->data_direction == DMA_TO_DEVICE) {
			iscsit_stop_dataout_timer(cmd);
			/*
			 * Check for special case during comp_err where
			 * WRITE_PENDING has been handed off from core,
			 * but requires an extra target_put_sess_cmd()
			 * before transport_generic_free_cmd() below.
			 */
			if (comp_err &&
			    cmd->se_cmd.t_state == TRANSPORT_WRITE_PENDING) {
				struct se_cmd *se_cmd = &cmd->se_cmd;

				target_put_sess_cmd(se_cmd->se_sess, se_cmd);
			}
		}

		device->unreg_rdma_mem(isert_cmd, isert_conn);
		transport_generic_free_cmd(&cmd->se_cmd, 0);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		spin_lock_bh(&conn->cmd_lock);
		if (!list_empty(&cmd->i_conn_node))
			list_del_init(&cmd->i_conn_node);
		spin_unlock_bh(&conn->cmd_lock);

		transport_generic_free_cmd(&cmd->se_cmd, 0);
		break;
	case ISCSI_OP_REJECT:
	case ISCSI_OP_NOOP_OUT:
	case ISCSI_OP_TEXT:
		spin_lock_bh(&conn->cmd_lock);
		if (!list_empty(&cmd->i_conn_node))
			list_del_init(&cmd->i_conn_node);
		spin_unlock_bh(&conn->cmd_lock);

		/*
		 * Handle special case for REJECT when iscsi_add_reject*() has
		 * overwritten the original iscsi_opcode assignment, and the
		 * associated cmd->se_cmd needs to be released.
		 */
		if (cmd->se_cmd.se_tfo != NULL) {
			pr_debug("Calling transport_generic_free_cmd from"
				 " isert_put_cmd for 0x%02x\n",
				 cmd->iscsi_opcode);
			transport_generic_free_cmd(&cmd->se_cmd, 0);
			break;
		}
		/*
		 * Fall-through
		 */
	default:
		iscsit_release_cmd(cmd);
		break;
	}
}

static void
isert_unmap_tx_desc(struct iser_tx_desc *tx_desc, struct ib_device *ib_dev)
{
	if (tx_desc->dma_addr != 0) {
		pr_debug("Calling ib_dma_unmap_single for tx_desc->dma_addr\n");
		ib_dma_unmap_single(ib_dev, tx_desc->dma_addr,
				    ISER_HEADERS_LEN, DMA_TO_DEVICE);
		tx_desc->dma_addr = 0;
	}
}

static void
isert_completion_put(struct iser_tx_desc *tx_desc, struct isert_cmd *isert_cmd,
		     struct ib_device *ib_dev, bool comp_err)
{
	if (isert_cmd->pdu_buf_dma != 0) {
		pr_debug("Calling ib_dma_unmap_single for isert_cmd->pdu_buf_dma\n");
		ib_dma_unmap_single(ib_dev, isert_cmd->pdu_buf_dma,
				    isert_cmd->pdu_buf_len, DMA_TO_DEVICE);
		isert_cmd->pdu_buf_dma = 0;
	}

	isert_unmap_tx_desc(tx_desc, ib_dev);
	isert_put_cmd(isert_cmd, comp_err);
}

static int
isert_check_pi_status(struct se_cmd *se_cmd, struct ib_mr *sig_mr)
{
	struct ib_mr_status mr_status;
	int ret;

	ret = ib_check_mr_status(sig_mr, IB_MR_CHECK_SIG_STATUS, &mr_status);
	if (ret) {
		pr_err("ib_check_mr_status failed, ret %d\n", ret);
		goto fail_mr_status;
	}

	if (mr_status.fail_status & IB_MR_CHECK_SIG_STATUS) {
		u64 sec_offset_err;
		u32 block_size = se_cmd->se_dev->dev_attrib.block_size + 8;

		switch (mr_status.sig_err.err_type) {
		case IB_SIG_BAD_GUARD:
			se_cmd->pi_err = TCM_LOGICAL_BLOCK_GUARD_CHECK_FAILED;
			break;
		case IB_SIG_BAD_REFTAG:
			se_cmd->pi_err = TCM_LOGICAL_BLOCK_REF_TAG_CHECK_FAILED;
			break;
		case IB_SIG_BAD_APPTAG:
			se_cmd->pi_err = TCM_LOGICAL_BLOCK_APP_TAG_CHECK_FAILED;
			break;
		}
		sec_offset_err = mr_status.sig_err.sig_err_offset;
		do_div(sec_offset_err, block_size);
		se_cmd->bad_sector = sec_offset_err + se_cmd->t_task_lba;

		pr_err("isert: PI error found type %d at sector 0x%llx "
		       "expected 0x%x vs actual 0x%x\n",
		       mr_status.sig_err.err_type,
		       (unsigned long long)se_cmd->bad_sector,
		       mr_status.sig_err.expected,
		       mr_status.sig_err.actual);
		ret = 1;
	}

fail_mr_status:
	return ret;
}

static void
isert_completion_rdma_write(struct iser_tx_desc *tx_desc,
			    struct isert_cmd *isert_cmd)
{
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_conn *isert_conn = isert_cmd->conn;
	struct isert_device *device = isert_conn->conn_device;
	int ret = 0;

	if (wr->fr_desc && wr->fr_desc->ind & ISERT_PROTECTED) {
		ret = isert_check_pi_status(se_cmd,
					    wr->fr_desc->pi_ctx->sig_mr);
		wr->fr_desc->ind &= ~ISERT_PROTECTED;
	}

	device->unreg_rdma_mem(isert_cmd, isert_conn);
	wr->send_wr_num = 0;
	if (ret)
		transport_send_check_condition_and_sense(se_cmd,
							 se_cmd->pi_err, 0);
	else
		isert_put_response(isert_conn->conn, cmd);
}

static void
isert_completion_rdma_read(struct iser_tx_desc *tx_desc,
			   struct isert_cmd *isert_cmd)
{
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_conn *isert_conn = isert_cmd->conn;
	struct isert_device *device = isert_conn->conn_device;
	int ret = 0;

	if (wr->fr_desc && wr->fr_desc->ind & ISERT_PROTECTED) {
		ret = isert_check_pi_status(se_cmd,
					    wr->fr_desc->pi_ctx->sig_mr);
		wr->fr_desc->ind &= ~ISERT_PROTECTED;
	}

	iscsit_stop_dataout_timer(cmd);
	device->unreg_rdma_mem(isert_cmd, isert_conn);
	cmd->write_data_done = wr->data.len;
	wr->send_wr_num = 0;

	pr_debug("Cmd: %p RDMA_READ comp calling execute_cmd\n", isert_cmd);
	spin_lock_bh(&cmd->istate_lock);
	cmd->cmd_flags |= ICF_GOT_LAST_DATAOUT;
	cmd->i_state = ISTATE_RECEIVED_LAST_DATAOUT;
	spin_unlock_bh(&cmd->istate_lock);

	if (ret)
		transport_send_check_condition_and_sense(se_cmd,
							 se_cmd->pi_err, 0);
	else
		target_execute_cmd(se_cmd);
}

static void
isert_do_control_comp(struct work_struct *work)
{
	struct isert_cmd *isert_cmd = container_of(work,
			struct isert_cmd, comp_work);
	struct isert_conn *isert_conn = isert_cmd->conn;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;

	switch (cmd->i_state) {
	case ISTATE_SEND_TASKMGTRSP:
		pr_debug("Calling iscsit_tmr_post_handler >>>>>>>>>>>>>>>>>\n");

		atomic_dec(&isert_conn->post_send_buf_count);
		iscsit_tmr_post_handler(cmd, cmd->conn);

		cmd->i_state = ISTATE_SENT_STATUS;
		isert_completion_put(&isert_cmd->tx_desc, isert_cmd, ib_dev, false);
		break;
	case ISTATE_SEND_REJECT:
		pr_debug("Got isert_do_control_comp ISTATE_SEND_REJECT: >>>\n");
		atomic_dec(&isert_conn->post_send_buf_count);

		cmd->i_state = ISTATE_SENT_STATUS;
		isert_completion_put(&isert_cmd->tx_desc, isert_cmd, ib_dev, false);
		break;
	case ISTATE_SEND_LOGOUTRSP:
		pr_debug("Calling iscsit_logout_post_handler >>>>>>>>>>>>>>\n");

		atomic_dec(&isert_conn->post_send_buf_count);
		iscsit_logout_post_handler(cmd, cmd->conn);
		break;
	case ISTATE_SEND_TEXTRSP:
		atomic_dec(&isert_conn->post_send_buf_count);
		cmd->i_state = ISTATE_SENT_STATUS;
		isert_completion_put(&isert_cmd->tx_desc, isert_cmd, ib_dev, false);
		break;
	default:
		pr_err("Unknown do_control_comp i_state %d\n", cmd->i_state);
		dump_stack();
		break;
	}
}

static void
isert_response_completion(struct iser_tx_desc *tx_desc,
			  struct isert_cmd *isert_cmd,
			  struct isert_conn *isert_conn,
			  struct ib_device *ib_dev)
{
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;

	if (cmd->i_state == ISTATE_SEND_TASKMGTRSP ||
	    cmd->i_state == ISTATE_SEND_LOGOUTRSP ||
	    cmd->i_state == ISTATE_SEND_REJECT ||
	    cmd->i_state == ISTATE_SEND_TEXTRSP) {
		isert_unmap_tx_desc(tx_desc, ib_dev);

		INIT_WORK(&isert_cmd->comp_work, isert_do_control_comp);
		queue_work(isert_comp_wq, &isert_cmd->comp_work);
		return;
	}

	/**
	 * If send_wr_num is 0 this means that we got
	 * RDMA completion and we cleared it and we should
	 * simply decrement the response post. else the
	 * response is incorporated in send_wr_num, just
	 * sub it.
	 **/
	if (wr->send_wr_num)
		atomic_sub(wr->send_wr_num, &isert_conn->post_send_buf_count);
	else
		atomic_dec(&isert_conn->post_send_buf_count);

	cmd->i_state = ISTATE_SENT_STATUS;
	isert_completion_put(tx_desc, isert_cmd, ib_dev, false);
}

static void
isert_send_completion(struct iser_tx_desc *tx_desc,
		      struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct isert_cmd *isert_cmd = tx_desc->isert_cmd;
	struct isert_rdma_wr *wr;

	if (!isert_cmd) {
		atomic_dec(&isert_conn->post_send_buf_count);
		isert_unmap_tx_desc(tx_desc, ib_dev);
		return;
	}
	wr = &isert_cmd->rdma_wr;

	switch (wr->iser_ib_op) {
	case ISER_IB_RECV:
		pr_err("isert_send_completion: Got ISER_IB_RECV\n");
		dump_stack();
		break;
	case ISER_IB_SEND:
		pr_debug("isert_send_completion: Got ISER_IB_SEND\n");
		isert_response_completion(tx_desc, isert_cmd,
					  isert_conn, ib_dev);
		break;
	case ISER_IB_RDMA_WRITE:
		pr_debug("isert_send_completion: Got ISER_IB_RDMA_WRITE\n");
		atomic_sub(wr->send_wr_num, &isert_conn->post_send_buf_count);
		isert_completion_rdma_write(tx_desc, isert_cmd);
		break;
	case ISER_IB_RDMA_READ:
		pr_debug("isert_send_completion: Got ISER_IB_RDMA_READ:\n");

		atomic_sub(wr->send_wr_num, &isert_conn->post_send_buf_count);
		isert_completion_rdma_read(tx_desc, isert_cmd);
		break;
	default:
		pr_err("Unknown wr->iser_ib_op: 0x%02x\n", wr->iser_ib_op);
		dump_stack();
		break;
	}
}

static void
isert_cq_comp_err(void *desc, struct isert_conn *isert_conn, bool tx)
{
	if (tx) {
		struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
		struct isert_cmd *isert_cmd;

		isert_cmd = ((struct iser_tx_desc *)desc)->isert_cmd;
		if (!isert_cmd)
			isert_unmap_tx_desc(desc, ib_dev);
		else
			isert_completion_put(desc, isert_cmd, ib_dev, true);
		atomic_dec(&isert_conn->post_send_buf_count);
	} else {
		isert_conn->post_recv_buf_count--;
	}

	if (isert_conn->post_recv_buf_count == 0 &&
	    atomic_read(&isert_conn->post_send_buf_count) == 0) {
		struct iscsi_conn *conn = isert_conn->conn;

		if (conn->sess) {
			target_sess_cmd_list_set_waiting(conn->sess->se_sess);
			target_wait_for_sess_cmds(conn->sess->se_sess);
		}

		mutex_lock(&isert_conn->conn_mutex);
		isert_conn_terminate(isert_conn);
		mutex_unlock(&isert_conn->conn_mutex);

		iscsit_cause_connection_reinstatement(isert_conn->conn, 0);
		complete(&isert_conn->conn_wait_comp_err);
	}
}

static void
isert_cq_tx_work(struct work_struct *work)
{
	struct isert_cq_desc *cq_desc = container_of(work,
				struct isert_cq_desc, cq_tx_work);
	struct isert_device *device = cq_desc->device;
	int cq_index = cq_desc->cq_index;
	struct ib_cq *tx_cq = device->dev_tx_cq[cq_index];
	struct isert_conn *isert_conn;
	struct iser_tx_desc *tx_desc;
	struct ib_wc wc;

	while (ib_poll_cq(tx_cq, 1, &wc) == 1) {
		tx_desc = (struct iser_tx_desc *)(uintptr_t)wc.wr_id;
		isert_conn = wc.qp->qp_context;

		if (wc.status == IB_WC_SUCCESS) {
			isert_send_completion(tx_desc, isert_conn);
		} else {
			pr_debug("TX wc.status != IB_WC_SUCCESS >>>>>>>>>>>>>>\n");
			pr_debug("TX wc.status: 0x%08x\n", wc.status);
			pr_debug("TX wc.vendor_err: 0x%08x\n", wc.vendor_err);

			if (wc.wr_id != ISER_FASTREG_LI_WRID)
				isert_cq_comp_err(tx_desc, isert_conn, true);
		}
	}

	ib_req_notify_cq(tx_cq, IB_CQ_NEXT_COMP);
}

static void
isert_cq_tx_callback(struct ib_cq *cq, void *context)
{
	struct isert_cq_desc *cq_desc = (struct isert_cq_desc *)context;

	queue_work(isert_comp_wq, &cq_desc->cq_tx_work);
}

static void
isert_cq_rx_work(struct work_struct *work)
{
	struct isert_cq_desc *cq_desc = container_of(work,
			struct isert_cq_desc, cq_rx_work);
	struct isert_device *device = cq_desc->device;
	int cq_index = cq_desc->cq_index;
	struct ib_cq *rx_cq = device->dev_rx_cq[cq_index];
	struct isert_conn *isert_conn;
	struct iser_rx_desc *rx_desc;
	struct ib_wc wc;
	unsigned long xfer_len;

	while (ib_poll_cq(rx_cq, 1, &wc) == 1) {
		rx_desc = (struct iser_rx_desc *)(uintptr_t)wc.wr_id;
		isert_conn = wc.qp->qp_context;

		if (wc.status == IB_WC_SUCCESS) {
			xfer_len = (unsigned long)wc.byte_len;
			isert_rx_completion(rx_desc, isert_conn, xfer_len);
		} else {
			pr_debug("RX wc.status != IB_WC_SUCCESS >>>>>>>>>>>>>>\n");
			if (wc.status != IB_WC_WR_FLUSH_ERR) {
				pr_debug("RX wc.status: 0x%08x\n", wc.status);
				pr_debug("RX wc.vendor_err: 0x%08x\n",
					 wc.vendor_err);
			}
			isert_cq_comp_err(rx_desc, isert_conn, false);
		}
	}

	ib_req_notify_cq(rx_cq, IB_CQ_NEXT_COMP);
}

static void
isert_cq_rx_callback(struct ib_cq *cq, void *context)
{
	struct isert_cq_desc *cq_desc = (struct isert_cq_desc *)context;

	queue_work(isert_rx_wq, &cq_desc->cq_rx_work);
}

static int
isert_post_response(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd)
{
	struct ib_send_wr *wr_failed;
	int ret;

	atomic_inc(&isert_conn->post_send_buf_count);

	ret = ib_post_send(isert_conn->conn_qp, &isert_cmd->tx_desc.send_wr,
			   &wr_failed);
	if (ret) {
		pr_err("ib_post_send failed with %d\n", ret);
		atomic_dec(&isert_conn->post_send_buf_count);
		return ret;
	}
	return ret;
}

static int
isert_put_response(struct iscsi_conn *conn, struct iscsi_cmd *cmd)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;
	struct iscsi_scsi_rsp *hdr = (struct iscsi_scsi_rsp *)
				&isert_cmd->tx_desc.iscsi_header;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_rsp_pdu(cmd, conn, true, hdr);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	/*
	 * Attach SENSE DATA payload to iSCSI Response PDU
	 */
	if (cmd->se_cmd.sense_buffer &&
	    ((cmd->se_cmd.se_cmd_flags & SCF_TRANSPORT_TASK_SENSE) ||
	    (cmd->se_cmd.se_cmd_flags & SCF_EMULATED_TASK_SENSE))) {
		struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
		struct ib_sge *tx_dsg = &isert_cmd->tx_desc.tx_sg[1];
		u32 padding, pdu_len;

		put_unaligned_be16(cmd->se_cmd.scsi_sense_length,
				   cmd->sense_buffer);
		cmd->se_cmd.scsi_sense_length += sizeof(__be16);

		padding = -(cmd->se_cmd.scsi_sense_length) & 3;
		hton24(hdr->dlength, (u32)cmd->se_cmd.scsi_sense_length);
		pdu_len = cmd->se_cmd.scsi_sense_length + padding;

		isert_cmd->pdu_buf_dma = ib_dma_map_single(ib_dev,
				(void *)cmd->sense_buffer, pdu_len,
				DMA_TO_DEVICE);

		isert_cmd->pdu_buf_len = pdu_len;
		tx_dsg->addr	= isert_cmd->pdu_buf_dma;
		tx_dsg->length	= pdu_len;
		tx_dsg->lkey	= isert_conn->conn_mr->lkey;
		isert_cmd->tx_desc.num_sge = 2;
	}

	isert_init_send_wr(isert_conn, isert_cmd, send_wr);

	pr_debug("Posting SCSI Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static void
isert_aborted_task(struct iscsi_conn *conn, struct iscsi_cmd *cmd)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct isert_device *device = isert_conn->conn_device;

	spin_lock_bh(&conn->cmd_lock);
	if (!list_empty(&cmd->i_conn_node))
		list_del_init(&cmd->i_conn_node);
	spin_unlock_bh(&conn->cmd_lock);

	if (cmd->data_direction == DMA_TO_DEVICE)
		iscsit_stop_dataout_timer(cmd);

	device->unreg_rdma_mem(isert_cmd, isert_conn);
}

static enum target_prot_op
isert_get_sup_prot_ops(struct iscsi_conn *conn)
{
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct isert_device *device = isert_conn->conn_device;

	if (conn->tpg->tpg_attrib.t10_pi) {
		if (device->pi_capable) {
			pr_info("conn %p PI offload enabled\n", isert_conn);
			isert_conn->pi_support = true;
			return TARGET_PROT_ALL;
		}
	}

	pr_info("conn %p PI offload disabled\n", isert_conn);
	isert_conn->pi_support = false;

	return TARGET_PROT_NORMAL;
}

static int
isert_put_nopin(struct iscsi_cmd *cmd, struct iscsi_conn *conn,
		bool nopout_response)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_nopin_rsp(cmd, conn, (struct iscsi_nopin *)
			       &isert_cmd->tx_desc.iscsi_header,
			       nopout_response);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_conn, isert_cmd, send_wr);

	pr_debug("Posting NOPIN Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_logout_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_logout_rsp(cmd, conn, (struct iscsi_logout_rsp *)
				&isert_cmd->tx_desc.iscsi_header);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_conn, isert_cmd, send_wr);

	pr_debug("Posting Logout Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_tm_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_task_mgt_rsp(cmd, conn, (struct iscsi_tm_rsp *)
				  &isert_cmd->tx_desc.iscsi_header);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_conn, isert_cmd, send_wr);

	pr_debug("Posting Task Management Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_reject(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct ib_sge *tx_dsg = &isert_cmd->tx_desc.tx_sg[1];
	struct iscsi_reject *hdr =
		(struct iscsi_reject *)&isert_cmd->tx_desc.iscsi_header;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_reject(cmd, conn, hdr);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);

	hton24(hdr->dlength, ISCSI_HDR_LEN);
	isert_cmd->pdu_buf_dma = ib_dma_map_single(ib_dev,
			(void *)cmd->buf_ptr, ISCSI_HDR_LEN,
			DMA_TO_DEVICE);
	isert_cmd->pdu_buf_len = ISCSI_HDR_LEN;
	tx_dsg->addr	= isert_cmd->pdu_buf_dma;
	tx_dsg->length	= ISCSI_HDR_LEN;
	tx_dsg->lkey	= isert_conn->conn_mr->lkey;
	isert_cmd->tx_desc.num_sge = 2;

	isert_init_send_wr(isert_conn, isert_cmd, send_wr);

	pr_debug("Posting Reject IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_text_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;
	struct iscsi_text_rsp *hdr =
		(struct iscsi_text_rsp *)&isert_cmd->tx_desc.iscsi_header;
	u32 txt_rsp_len;
	int rc;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	rc = iscsit_build_text_rsp(cmd, conn, hdr, ISCSI_INFINIBAND);
	if (rc < 0)
		return rc;

	txt_rsp_len = rc;
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);

	if (txt_rsp_len) {
		struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
		struct ib_sge *tx_dsg = &isert_cmd->tx_desc.tx_sg[1];
		void *txt_rsp_buf = cmd->buf_ptr;

		isert_cmd->pdu_buf_dma = ib_dma_map_single(ib_dev,
				txt_rsp_buf, txt_rsp_len, DMA_TO_DEVICE);

		isert_cmd->pdu_buf_len = txt_rsp_len;
		tx_dsg->addr	= isert_cmd->pdu_buf_dma;
		tx_dsg->length	= txt_rsp_len;
		tx_dsg->lkey	= isert_conn->conn_mr->lkey;
		isert_cmd->tx_desc.num_sge = 2;
	}
	isert_init_send_wr(isert_conn, isert_cmd, send_wr);

	pr_debug("Posting Text Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_build_rdma_wr(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd,
		    struct ib_sge *ib_sge, struct ib_send_wr *send_wr,
		    u32 data_left, u32 offset)
{
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;
	struct scatterlist *sg_start, *tmp_sg;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	u32 sg_off, page_off;
	int i = 0, sg_nents;

	sg_off = offset / PAGE_SIZE;
	sg_start = &cmd->se_cmd.t_data_sg[sg_off];
	sg_nents = min(cmd->se_cmd.t_data_nents - sg_off, isert_conn->max_sge);
	page_off = offset % PAGE_SIZE;

	send_wr->sg_list = ib_sge;
	send_wr->num_sge = sg_nents;
	send_wr->wr_id = (uintptr_t)&isert_cmd->tx_desc;
	/*
	 * Perform mapping of TCM scatterlist memory ib_sge dma_addr.
	 */
	for_each_sg(sg_start, tmp_sg, sg_nents, i) {
		pr_debug("ISER RDMA from SGL dma_addr: 0x%16llx dma_len: %u, page_off: %u\n",
			 (unsigned long long)tmp_sg->dma_address,
			 tmp_sg->length, page_off);

		ib_sge->addr = ib_sg_dma_address(ib_dev, tmp_sg) + page_off;
		ib_sge->length = min_t(u32, data_left,
				ib_sg_dma_len(ib_dev, tmp_sg) - page_off);
		ib_sge->lkey = isert_conn->conn_mr->lkey;

		pr_debug("RDMA ib_sge: addr: 0x%16llx  length: %u lkey: %08x\n",
			 ib_sge->addr, ib_sge->length, ib_sge->lkey);
		page_off = 0;
		data_left -= ib_sge->length;
		ib_sge++;
		pr_debug("Incrementing ib_sge pointer to %p\n", ib_sge);
	}

	pr_debug("Set outgoing sg_list: %p num_sg: %u from TCM SGLs\n",
		 send_wr->sg_list, send_wr->num_sge);

	return sg_nents;
}

static int
isert_map_rdma(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
	       struct isert_rdma_wr *wr)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct isert_data_buf *data = &wr->data;
	struct ib_send_wr *send_wr;
	struct ib_sge *ib_sge;
	u32 offset, data_len, data_left, rdma_write_max, va_offset = 0;
	int ret = 0, i, ib_sge_cnt;

	isert_cmd->tx_desc.isert_cmd = isert_cmd;

	offset = wr->iser_ib_op == ISER_IB_RDMA_READ ? cmd->write_data_done : 0;
	ret = isert_map_data_buf(isert_conn, isert_cmd, se_cmd->t_data_sg,
				 se_cmd->t_data_nents, se_cmd->data_length,
				 offset, wr->iser_ib_op, &wr->data);
	if (ret)
		return ret;

	data_left = data->len;
	offset = data->offset;

	ib_sge = kzalloc(sizeof(struct ib_sge) * data->nents, GFP_KERNEL);
	if (!ib_sge) {
		pr_warn("Unable to allocate ib_sge\n");
		ret = -ENOMEM;
		goto unmap_cmd;
	}
	wr->ib_sge = ib_sge;

	wr->send_wr_num = DIV_ROUND_UP(data->nents, isert_conn->max_sge);
	wr->send_wr = kzalloc(sizeof(struct ib_send_wr) * wr->send_wr_num,
				GFP_KERNEL);
	if (!wr->send_wr) {
		pr_debug("Unable to allocate wr->send_wr\n");
		ret = -ENOMEM;
		goto unmap_cmd;
	}

	wr->isert_cmd = isert_cmd;
	rdma_write_max = isert_conn->max_sge * PAGE_SIZE;

	for (i = 0; i < wr->send_wr_num; i++) {
		send_wr = &isert_cmd->rdma_wr.send_wr[i];
		data_len = min(data_left, rdma_write_max);

		send_wr->send_flags = 0;
		if (wr->iser_ib_op == ISER_IB_RDMA_WRITE) {
			send_wr->opcode = IB_WR_RDMA_WRITE;
			send_wr->wr.rdma.remote_addr = isert_cmd->read_va + offset;
			send_wr->wr.rdma.rkey = isert_cmd->read_stag;
			if (i + 1 == wr->send_wr_num)
				send_wr->next = &isert_cmd->tx_desc.send_wr;
			else
				send_wr->next = &wr->send_wr[i + 1];
		} else {
			send_wr->opcode = IB_WR_RDMA_READ;
			send_wr->wr.rdma.remote_addr = isert_cmd->write_va + va_offset;
			send_wr->wr.rdma.rkey = isert_cmd->write_stag;
			if (i + 1 == wr->send_wr_num)
				send_wr->send_flags = IB_SEND_SIGNALED;
			else
				send_wr->next = &wr->send_wr[i + 1];
		}

		ib_sge_cnt = isert_build_rdma_wr(isert_conn, isert_cmd, ib_sge,
					send_wr, data_len, offset);
		ib_sge += ib_sge_cnt;

		offset += data_len;
		va_offset += data_len;
		data_left -= data_len;
	}

	return 0;
unmap_cmd:
	isert_unmap_data_buf(isert_conn, data);

	return ret;
}

static int
isert_map_fr_pagelist(struct ib_device *ib_dev,
		      struct scatterlist *sg_start, int sg_nents, u64 *fr_pl)
{
	u64 start_addr, end_addr, page, chunk_start = 0;
	struct scatterlist *tmp_sg;
	int i = 0, new_chunk, last_ent, n_pages;

	n_pages = 0;
	new_chunk = 1;
	last_ent = sg_nents - 1;
	for_each_sg(sg_start, tmp_sg, sg_nents, i) {
		start_addr = ib_sg_dma_address(ib_dev, tmp_sg);
		if (new_chunk)
			chunk_start = start_addr;
		end_addr = start_addr + ib_sg_dma_len(ib_dev, tmp_sg);

		pr_debug("SGL[%d] dma_addr: 0x%16llx len: %u\n",
			 i, (unsigned long long)tmp_sg->dma_address,
			 tmp_sg->length);

		if ((end_addr & ~PAGE_MASK) && i < last_ent) {
			new_chunk = 0;
			continue;
		}
		new_chunk = 1;

		page = chunk_start & PAGE_MASK;
		do {
			fr_pl[n_pages++] = page;
			pr_debug("Mapped page_list[%d] page_addr: 0x%16llx\n",
				 n_pages - 1, page);
			page += PAGE_SIZE;
		} while (page < end_addr);
	}

	return n_pages;
}

static int
isert_fast_reg_mr(struct isert_conn *isert_conn,
		  struct fast_reg_descriptor *fr_desc,
		  struct isert_data_buf *mem,
		  enum isert_indicator ind,
		  struct ib_sge *sge)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct ib_mr *mr;
	struct ib_fast_reg_page_list *frpl;
	struct ib_send_wr fr_wr, inv_wr;
	struct ib_send_wr *bad_wr, *wr = NULL;
	int ret, pagelist_len;
	u32 page_off;
	u8 key;

	if (mem->dma_nents == 1) {
		sge->lkey = isert_conn->conn_mr->lkey;
		sge->addr = ib_sg_dma_address(ib_dev, &mem->sg[0]);
		sge->length = ib_sg_dma_len(ib_dev, &mem->sg[0]);
		pr_debug("%s:%d sge: addr: 0x%llx  length: %u lkey: %x\n",
			 __func__, __LINE__, sge->addr, sge->length,
			 sge->lkey);
		return 0;
	}

	if (ind == ISERT_DATA_KEY_VALID) {
		/* Registering data buffer */
		mr = fr_desc->data_mr;
		frpl = fr_desc->data_frpl;
	} else {
		/* Registering protection buffer */
		mr = fr_desc->pi_ctx->prot_mr;
		frpl = fr_desc->pi_ctx->prot_frpl;
	}

	page_off = mem->offset % PAGE_SIZE;

	pr_debug("Use fr_desc %p sg_nents %d offset %u\n",
		 fr_desc, mem->nents, mem->offset);

	pagelist_len = isert_map_fr_pagelist(ib_dev, mem->sg, mem->nents,
					     &frpl->page_list[0]);

	if (!(fr_desc->ind & ISERT_DATA_KEY_VALID)) {
		memset(&inv_wr, 0, sizeof(inv_wr));
		inv_wr.wr_id = ISER_FASTREG_LI_WRID;
		inv_wr.opcode = IB_WR_LOCAL_INV;
		inv_wr.ex.invalidate_rkey = mr->rkey;
		wr = &inv_wr;
		/* Bump the key */
		key = (u8)(mr->rkey & 0x000000FF);
		ib_update_fast_reg_key(mr, ++key);
	}

	/* Prepare FASTREG WR */
	memset(&fr_wr, 0, sizeof(fr_wr));
	fr_wr.wr_id = ISER_FASTREG_LI_WRID;
	fr_wr.opcode = IB_WR_FAST_REG_MR;
	fr_wr.wr.fast_reg.iova_start = frpl->page_list[0] + page_off;
	fr_wr.wr.fast_reg.page_list = frpl;
	fr_wr.wr.fast_reg.page_list_len = pagelist_len;
	fr_wr.wr.fast_reg.page_shift = PAGE_SHIFT;
	fr_wr.wr.fast_reg.length = mem->len;
	fr_wr.wr.fast_reg.rkey = mr->rkey;
	fr_wr.wr.fast_reg.access_flags = IB_ACCESS_LOCAL_WRITE;

	if (!wr)
		wr = &fr_wr;
	else
		wr->next = &fr_wr;

	ret = ib_post_send(isert_conn->conn_qp, wr, &bad_wr);
	if (ret) {
		pr_err("fast registration failed, ret:%d\n", ret);
		return ret;
	}
	fr_desc->ind &= ~ind;

	sge->lkey = mr->lkey;
	sge->addr = frpl->page_list[0] + page_off;
	sge->length = mem->len;

	pr_debug("%s:%d sge: addr: 0x%llx  length: %u lkey: %x\n",
		 __func__, __LINE__, sge->addr, sge->length,
		 sge->lkey);

	return ret;
}

static inline void
isert_set_dif_domain(struct se_cmd *se_cmd, struct ib_sig_attrs *sig_attrs,
		     struct ib_sig_domain *domain)
{
	domain->sig_type = IB_SIG_TYPE_T10_DIF;
	domain->sig.dif.bg_type = IB_T10DIF_CRC;
	domain->sig.dif.pi_interval = se_cmd->se_dev->dev_attrib.block_size;
	domain->sig.dif.ref_tag = se_cmd->reftag_seed;
	/*
	 * At the moment we hard code those, but if in the future
	 * the target core would like to use it, we will take it
	 * from se_cmd.
	 */
	domain->sig.dif.apptag_check_mask = 0xffff;
	domain->sig.dif.app_escape = true;
	domain->sig.dif.ref_escape = true;
	if (se_cmd->prot_type == TARGET_DIF_TYPE1_PROT ||
	    se_cmd->prot_type == TARGET_DIF_TYPE2_PROT)
		domain->sig.dif.ref_remap = true;
};

static int
isert_set_sig_attrs(struct se_cmd *se_cmd, struct ib_sig_attrs *sig_attrs)
{
	switch (se_cmd->prot_op) {
	case TARGET_PROT_DIN_INSERT:
	case TARGET_PROT_DOUT_STRIP:
		sig_attrs->mem.sig_type = IB_SIG_TYPE_NONE;
		isert_set_dif_domain(se_cmd, sig_attrs, &sig_attrs->wire);
		break;
	case TARGET_PROT_DOUT_INSERT:
	case TARGET_PROT_DIN_STRIP:
		sig_attrs->wire.sig_type = IB_SIG_TYPE_NONE;
		isert_set_dif_domain(se_cmd, sig_attrs, &sig_attrs->mem);
		break;
	case TARGET_PROT_DIN_PASS:
	case TARGET_PROT_DOUT_PASS:
		isert_set_dif_domain(se_cmd, sig_attrs, &sig_attrs->wire);
		isert_set_dif_domain(se_cmd, sig_attrs, &sig_attrs->mem);
		break;
	default:
		pr_err("Unsupported PI operation %d\n", se_cmd->prot_op);
		return -EINVAL;
	}

	return 0;
}

static inline u8
isert_set_prot_checks(u8 prot_checks)
{
	return (prot_checks & TARGET_DIF_CHECK_GUARD  ? 0xc0 : 0) |
	       (prot_checks & TARGET_DIF_CHECK_REFTAG ? 0x30 : 0) |
	       (prot_checks & TARGET_DIF_CHECK_REFTAG ? 0x0f : 0);
}

static int
isert_reg_sig_mr(struct isert_conn *isert_conn,
		 struct se_cmd *se_cmd,
		 struct isert_rdma_wr *rdma_wr,
		 struct fast_reg_descriptor *fr_desc)
{
	struct ib_send_wr sig_wr, inv_wr;
	struct ib_send_wr *bad_wr, *wr = NULL;
	struct pi_context *pi_ctx = fr_desc->pi_ctx;
	struct ib_sig_attrs sig_attrs;
	int ret;
	u32 key;

	memset(&sig_attrs, 0, sizeof(sig_attrs));
	ret = isert_set_sig_attrs(se_cmd, &sig_attrs);
	if (ret)
		goto err;

	sig_attrs.check_mask = isert_set_prot_checks(se_cmd->prot_checks);

	if (!(fr_desc->ind & ISERT_SIG_KEY_VALID)) {
		memset(&inv_wr, 0, sizeof(inv_wr));
		inv_wr.opcode = IB_WR_LOCAL_INV;
		inv_wr.wr_id = ISER_FASTREG_LI_WRID;
		inv_wr.ex.invalidate_rkey = pi_ctx->sig_mr->rkey;
		wr = &inv_wr;
		/* Bump the key */
		key = (u8)(pi_ctx->sig_mr->rkey & 0x000000FF);
		ib_update_fast_reg_key(pi_ctx->sig_mr, ++key);
	}

	memset(&sig_wr, 0, sizeof(sig_wr));
	sig_wr.opcode = IB_WR_REG_SIG_MR;
	sig_wr.wr_id = ISER_FASTREG_LI_WRID;
	sig_wr.sg_list = &rdma_wr->ib_sg[DATA];
	sig_wr.num_sge = 1;
	sig_wr.wr.sig_handover.access_flags = IB_ACCESS_LOCAL_WRITE;
	sig_wr.wr.sig_handover.sig_attrs = &sig_attrs;
	sig_wr.wr.sig_handover.sig_mr = pi_ctx->sig_mr;
	if (se_cmd->t_prot_sg)
		sig_wr.wr.sig_handover.prot = &rdma_wr->ib_sg[PROT];

	if (!wr)
		wr = &sig_wr;
	else
		wr->next = &sig_wr;

	ret = ib_post_send(isert_conn->conn_qp, wr, &bad_wr);
	if (ret) {
		pr_err("fast registration failed, ret:%d\n", ret);
		goto err;
	}
	fr_desc->ind &= ~ISERT_SIG_KEY_VALID;

	rdma_wr->ib_sg[SIG].lkey = pi_ctx->sig_mr->lkey;
	rdma_wr->ib_sg[SIG].addr = 0;
	rdma_wr->ib_sg[SIG].length = se_cmd->data_length;
	if (se_cmd->prot_op != TARGET_PROT_DIN_STRIP &&
	    se_cmd->prot_op != TARGET_PROT_DOUT_INSERT)
		/*
		 * We have protection guards on the wire
		 * so we need to set a larget transfer
		 */
		rdma_wr->ib_sg[SIG].length += se_cmd->prot_length;

	pr_debug("sig_sge: addr: 0x%llx  length: %u lkey: %x\n",
		  rdma_wr->ib_sg[SIG].addr, rdma_wr->ib_sg[SIG].length,
		  rdma_wr->ib_sg[SIG].lkey);
err:
	return ret;
}

static int
isert_handle_prot_cmd(struct isert_conn *isert_conn,
		      struct isert_cmd *isert_cmd,
		      struct isert_rdma_wr *wr)
{
	struct isert_device *device = isert_conn->conn_device;
	struct se_cmd *se_cmd = &isert_cmd->iscsi_cmd->se_cmd;
	int ret;

	if (!wr->fr_desc->pi_ctx) {
		ret = isert_create_pi_ctx(wr->fr_desc,
					  device->ib_device,
					  isert_conn->conn_pd);
		if (ret) {
			pr_err("conn %p failed to allocate pi_ctx\n",
				  isert_conn);
			return ret;
		}
	}

	if (se_cmd->t_prot_sg) {
		ret = isert_map_data_buf(isert_conn, isert_cmd,
					 se_cmd->t_prot_sg,
					 se_cmd->t_prot_nents,
					 se_cmd->prot_length,
					 0, wr->iser_ib_op, &wr->prot);
		if (ret) {
			pr_err("conn %p failed to map protection buffer\n",
				  isert_conn);
			return ret;
		}

		memset(&wr->ib_sg[PROT], 0, sizeof(wr->ib_sg[PROT]));
		ret = isert_fast_reg_mr(isert_conn, wr->fr_desc, &wr->prot,
					ISERT_PROT_KEY_VALID, &wr->ib_sg[PROT]);
		if (ret) {
			pr_err("conn %p failed to fast reg mr\n",
				  isert_conn);
			goto unmap_prot_cmd;
		}
	}

	ret = isert_reg_sig_mr(isert_conn, se_cmd, wr, wr->fr_desc);
	if (ret) {
		pr_err("conn %p failed to fast reg mr\n",
			  isert_conn);
		goto unmap_prot_cmd;
	}
	wr->fr_desc->ind |= ISERT_PROTECTED;

	return 0;

unmap_prot_cmd:
	if (se_cmd->t_prot_sg)
		isert_unmap_data_buf(isert_conn, &wr->prot);

	return ret;
}

static int
isert_reg_rdma(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
	       struct isert_rdma_wr *wr)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = conn->context;
	struct fast_reg_descriptor *fr_desc = NULL;
	struct ib_send_wr *send_wr;
	struct ib_sge *ib_sg;
	u32 offset;
	int ret = 0;
	unsigned long flags;

	isert_cmd->tx_desc.isert_cmd = isert_cmd;

	offset = wr->iser_ib_op == ISER_IB_RDMA_READ ? cmd->write_data_done : 0;
	ret = isert_map_data_buf(isert_conn, isert_cmd, se_cmd->t_data_sg,
				 se_cmd->t_data_nents, se_cmd->data_length,
				 offset, wr->iser_ib_op, &wr->data);
	if (ret)
		return ret;

	if (wr->data.dma_nents != 1 || isert_prot_cmd(isert_conn, se_cmd)) {
		spin_lock_irqsave(&isert_conn->conn_lock, flags);
		fr_desc = list_first_entry(&isert_conn->conn_fr_pool,
					   struct fast_reg_descriptor, list);
		list_del(&fr_desc->list);
		spin_unlock_irqrestore(&isert_conn->conn_lock, flags);
		wr->fr_desc = fr_desc;
	}

	ret = isert_fast_reg_mr(isert_conn, fr_desc, &wr->data,
				ISERT_DATA_KEY_VALID, &wr->ib_sg[DATA]);
	if (ret)
		goto unmap_cmd;

	if (isert_prot_cmd(isert_conn, se_cmd)) {
		ret = isert_handle_prot_cmd(isert_conn, isert_cmd, wr);
		if (ret)
			goto unmap_cmd;

		ib_sg = &wr->ib_sg[SIG];
	} else {
		ib_sg = &wr->ib_sg[DATA];
	}

	memcpy(&wr->s_ib_sge, ib_sg, sizeof(*ib_sg));
	wr->ib_sge = &wr->s_ib_sge;
	wr->send_wr_num = 1;
	memset(&wr->s_send_wr, 0, sizeof(*send_wr));
	wr->send_wr = &wr->s_send_wr;
	wr->isert_cmd = isert_cmd;

	send_wr = &isert_cmd->rdma_wr.s_send_wr;
	send_wr->sg_list = &wr->s_ib_sge;
	send_wr->num_sge = 1;
	send_wr->wr_id = (uintptr_t)&isert_cmd->tx_desc;
	if (wr->iser_ib_op == ISER_IB_RDMA_WRITE) {
		send_wr->opcode = IB_WR_RDMA_WRITE;
		send_wr->wr.rdma.remote_addr = isert_cmd->read_va;
		send_wr->wr.rdma.rkey = isert_cmd->read_stag;
		send_wr->send_flags = !isert_prot_cmd(isert_conn, se_cmd) ?
				      0 : IB_SEND_SIGNALED;
	} else {
		send_wr->opcode = IB_WR_RDMA_READ;
		send_wr->wr.rdma.remote_addr = isert_cmd->write_va;
		send_wr->wr.rdma.rkey = isert_cmd->write_stag;
		send_wr->send_flags = IB_SEND_SIGNALED;
	}

	return 0;

unmap_cmd:
	if (fr_desc) {
		spin_lock_irqsave(&isert_conn->conn_lock, flags);
		list_add_tail(&fr_desc->list, &isert_conn->conn_fr_pool);
		spin_unlock_irqrestore(&isert_conn->conn_lock, flags);
	}
	isert_unmap_data_buf(isert_conn, &wr->data);

	return ret;
}

static int
isert_put_datain(struct iscsi_conn *conn, struct iscsi_cmd *cmd)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct isert_device *device = isert_conn->conn_device;
	struct ib_send_wr *wr_failed;
	int rc;

	pr_debug("Cmd: %p RDMA_WRITE data_length: %u\n",
		 isert_cmd, se_cmd->data_length);
	wr->iser_ib_op = ISER_IB_RDMA_WRITE;
	rc = device->reg_rdma_mem(conn, cmd, wr);
	if (rc) {
		pr_err("Cmd: %p failed to prepare RDMA res\n", isert_cmd);
		return rc;
	}

	if (!isert_prot_cmd(isert_conn, se_cmd)) {
		/*
		 * Build isert_conn->tx_desc for iSCSI response PDU and attach
		 */
		isert_create_send_desc(isert_conn, isert_cmd,
				       &isert_cmd->tx_desc);
		iscsit_build_rsp_pdu(cmd, conn, true, (struct iscsi_scsi_rsp *)
				     &isert_cmd->tx_desc.iscsi_header);
		isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
		isert_init_send_wr(isert_conn, isert_cmd,
				   &isert_cmd->tx_desc.send_wr);
		isert_cmd->rdma_wr.s_send_wr.next = &isert_cmd->tx_desc.send_wr;
		wr->send_wr_num += 1;
	}

	atomic_add(wr->send_wr_num, &isert_conn->post_send_buf_count);

	rc = ib_post_send(isert_conn->conn_qp, wr->send_wr, &wr_failed);
	if (rc) {
		pr_warn("ib_post_send() failed for IB_WR_RDMA_WRITE\n");
		atomic_sub(wr->send_wr_num, &isert_conn->post_send_buf_count);
	}

	if (!isert_prot_cmd(isert_conn, se_cmd))
		pr_debug("Cmd: %p posted RDMA_WRITE + Response for iSER Data "
			 "READ\n", isert_cmd);
	else
		pr_debug("Cmd: %p posted RDMA_WRITE for iSER Data READ\n",
			 isert_cmd);

	return 1;
}

static int
isert_get_dataout(struct iscsi_conn *conn, struct iscsi_cmd *cmd, bool recovery)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct isert_device *device = isert_conn->conn_device;
	struct ib_send_wr *wr_failed;
	int rc;

	pr_debug("Cmd: %p RDMA_READ data_length: %u write_data_done: %u\n",
		 isert_cmd, se_cmd->data_length, cmd->write_data_done);
	wr->iser_ib_op = ISER_IB_RDMA_READ;
	rc = device->reg_rdma_mem(conn, cmd, wr);
	if (rc) {
		pr_err("Cmd: %p failed to prepare RDMA res\n", isert_cmd);
		return rc;
	}

	atomic_add(wr->send_wr_num, &isert_conn->post_send_buf_count);

	rc = ib_post_send(isert_conn->conn_qp, wr->send_wr, &wr_failed);
	if (rc) {
		pr_warn("ib_post_send() failed for IB_WR_RDMA_READ\n");
		atomic_sub(wr->send_wr_num, &isert_conn->post_send_buf_count);
	}
	pr_debug("Cmd: %p posted RDMA_READ memory for ISER Data WRITE\n",
		 isert_cmd);

	return 0;
}

static int
isert_immediate_queue(struct iscsi_conn *conn, struct iscsi_cmd *cmd, int state)
{
	int ret;

	switch (state) {
	case ISTATE_SEND_NOPIN_WANT_RESPONSE:
		ret = isert_put_nopin(cmd, conn, false);
		break;
	default:
		pr_err("Unknown immediate state: 0x%02x\n", state);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int
isert_response_queue(struct iscsi_conn *conn, struct iscsi_cmd *cmd, int state)
{
	int ret;

	switch (state) {
	case ISTATE_SEND_LOGOUTRSP:
		ret = isert_put_logout_rsp(cmd, conn);
		if (!ret) {
			pr_debug("Returning iSER Logout -EAGAIN\n");
			ret = -EAGAIN;
		}
		break;
	case ISTATE_SEND_NOPIN:
		ret = isert_put_nopin(cmd, conn, true);
		break;
	case ISTATE_SEND_TASKMGTRSP:
		ret = isert_put_tm_rsp(cmd, conn);
		break;
	case ISTATE_SEND_REJECT:
		ret = isert_put_reject(cmd, conn);
		break;
	case ISTATE_SEND_TEXTRSP:
		ret = isert_put_text_rsp(cmd, conn);
		break;
	case ISTATE_SEND_STATUS:
		/*
		 * Special case for sending non GOOD SCSI status from TX thread
		 * context during pre se_cmd excecution failure.
		 */
		ret = isert_put_response(conn, cmd);
		break;
	default:
		pr_err("Unknown response state: 0x%02x\n", state);
		ret = -EINVAL;
		break;
	}

	return ret;
}

struct rdma_cm_id *
isert_setup_id(struct isert_np *isert_np)
{
	struct iscsi_np *np = isert_np->np;
	struct rdma_cm_id *id;
	struct sockaddr *sa;
	int ret;

	sa = (struct sockaddr *)&np->np_sockaddr;
	pr_debug("ksockaddr: %p, sa: %p\n", &np->np_sockaddr, sa);

	id = rdma_create_id(isert_cma_handler, isert_np,
			    RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(id)) {
		pr_err("rdma_create_id() failed: %ld\n", PTR_ERR(id));
		ret = PTR_ERR(id);
		goto out;
	}
	pr_debug("id %p context %p\n", id, id->context);

	ret = rdma_bind_addr(id, sa);
	if (ret) {
		pr_err("rdma_bind_addr() failed: %d\n", ret);
		goto out_id;
	}

	ret = rdma_listen(id, ISERT_RDMA_LISTEN_BACKLOG);
	if (ret) {
		pr_err("rdma_listen() failed: %d\n", ret);
		goto out_id;
	}

	return id;
out_id:
	rdma_destroy_id(id);
out:
	return ERR_PTR(ret);
}

static int
isert_setup_np(struct iscsi_np *np,
	       struct __kernel_sockaddr_storage *ksockaddr)
{
	struct isert_np *isert_np;
	struct rdma_cm_id *isert_lid;
	int ret;

	isert_np = kzalloc(sizeof(struct isert_np), GFP_KERNEL);
	if (!isert_np) {
		pr_err("Unable to allocate struct isert_np\n");
		return -ENOMEM;
	}
	sema_init(&isert_np->np_sem, 0);
	mutex_init(&isert_np->np_accept_mutex);
	INIT_LIST_HEAD(&isert_np->np_accept_list);
	init_completion(&isert_np->np_login_comp);
	isert_np->np = np;

	/*
	 * Setup the np->np_sockaddr from the passed sockaddr setup
	 * in iscsi_target_configfs.c code..
	 */
	memcpy(&np->np_sockaddr, ksockaddr,
	       sizeof(struct __kernel_sockaddr_storage));

	isert_lid = isert_setup_id(isert_np);
	if (IS_ERR(isert_lid)) {
		ret = PTR_ERR(isert_lid);
		goto out;
	}

	isert_np->np_cm_id = isert_lid;
	np->np_context = isert_np;

	return 0;

out:
	kfree(isert_np);

	return ret;
}

static int
isert_rdma_accept(struct isert_conn *isert_conn)
{
	struct rdma_cm_id *cm_id = isert_conn->conn_cm_id;
	struct rdma_conn_param cp;
	int ret;

	memset(&cp, 0, sizeof(struct rdma_conn_param));
	cp.initiator_depth = isert_conn->initiator_depth;
	cp.retry_count = 7;
	cp.rnr_retry_count = 7;

	pr_debug("Before rdma_accept >>>>>>>>>>>>>>>>>>>>.\n");

	ret = rdma_accept(cm_id, &cp);
	if (ret) {
		pr_err("rdma_accept() failed with: %d\n", ret);
		return ret;
	}

	pr_debug("After rdma_accept >>>>>>>>>>>>>>>>>>>>>.\n");

	return 0;
}

static int
isert_get_login_rx(struct iscsi_conn *conn, struct iscsi_login *login)
{
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	int ret;

	pr_info("before login_req comp conn: %p\n", isert_conn);
	ret = wait_for_completion_interruptible(&isert_conn->login_req_comp);
	if (ret) {
		pr_err("isert_conn %p interrupted before got login req\n",
			  isert_conn);
		return ret;
	}
	reinit_completion(&isert_conn->login_req_comp);

	/*
	 * For login requests after the first PDU, isert_rx_login_req() will
	 * kick schedule_delayed_work(&conn->login_work) as the packet is
	 * received, which turns this callback from iscsi_target_do_login_rx()
	 * into a NOP.
	 */
	if (!login->first_request)
		return 0;

	isert_rx_login_req(isert_conn);

	pr_info("before conn_login_comp conn: %p\n", conn);
	ret = wait_for_completion_interruptible(&isert_conn->conn_login_comp);
	if (ret)
		return ret;

	pr_info("processing login->req: %p\n", login->req);

	return 0;
}

static void
isert_set_conn_info(struct iscsi_np *np, struct iscsi_conn *conn,
		    struct isert_conn *isert_conn)
{
	struct rdma_cm_id *cm_id = isert_conn->conn_cm_id;
	struct rdma_route *cm_route = &cm_id->route;
	struct sockaddr_in *sock_in;
	struct sockaddr_in6 *sock_in6;

	conn->login_family = np->np_sockaddr.ss_family;

	if (np->np_sockaddr.ss_family == AF_INET6) {
		sock_in6 = (struct sockaddr_in6 *)&cm_route->addr.dst_addr;
		snprintf(conn->login_ip, sizeof(conn->login_ip), "%pI6c",
			 &sock_in6->sin6_addr.in6_u);
		conn->login_port = ntohs(sock_in6->sin6_port);

		sock_in6 = (struct sockaddr_in6 *)&cm_route->addr.src_addr;
		snprintf(conn->local_ip, sizeof(conn->local_ip), "%pI6c",
			 &sock_in6->sin6_addr.in6_u);
		conn->local_port = ntohs(sock_in6->sin6_port);
	} else {
		sock_in = (struct sockaddr_in *)&cm_route->addr.dst_addr;
		sprintf(conn->login_ip, "%pI4",
			&sock_in->sin_addr.s_addr);
		conn->login_port = ntohs(sock_in->sin_port);

		sock_in = (struct sockaddr_in *)&cm_route->addr.src_addr;
		sprintf(conn->local_ip, "%pI4",
			&sock_in->sin_addr.s_addr);
		conn->local_port = ntohs(sock_in->sin_port);
	}
}

static int
isert_accept_np(struct iscsi_np *np, struct iscsi_conn *conn)
{
	struct isert_np *isert_np = (struct isert_np *)np->np_context;
	struct isert_conn *isert_conn;
	int max_accept = 0, ret;

accept_wait:
	ret = down_interruptible(&isert_np->np_sem);
	if (ret || max_accept > 5)
		return -ENODEV;

	spin_lock_bh(&np->np_thread_lock);
	if (np->np_thread_state >= ISCSI_NP_THREAD_RESET) {
		spin_unlock_bh(&np->np_thread_lock);
		pr_debug("np_thread_state %d for isert_accept_np\n",
			 np->np_thread_state);
		/**
		 * No point in stalling here when np_thread
		 * is in state RESET/SHUTDOWN/EXIT - bail
		 **/
		return -ENODEV;
	}
	spin_unlock_bh(&np->np_thread_lock);

	mutex_lock(&isert_np->np_accept_mutex);
	if (list_empty(&isert_np->np_accept_list)) {
		mutex_unlock(&isert_np->np_accept_mutex);
		max_accept++;
		goto accept_wait;
	}
	isert_conn = list_first_entry(&isert_np->np_accept_list,
			struct isert_conn, conn_accept_node);
	list_del_init(&isert_conn->conn_accept_node);
	mutex_unlock(&isert_np->np_accept_mutex);

	conn->context = isert_conn;
	isert_conn->conn = conn;
	max_accept = 0;

	isert_set_conn_info(np, conn, isert_conn);

	pr_debug("Processing isert_conn: %p\n", isert_conn);

	return 0;
}

static void
isert_free_np(struct iscsi_np *np)
{
	struct isert_np *isert_np = (struct isert_np *)np->np_context;
	struct isert_conn *isert_conn, *n;

	if (isert_np->np_cm_id)
		rdma_destroy_id(isert_np->np_cm_id);

	/*
	 * FIXME: At this point we don't have a good way to insure
	 * that at this point we don't have hanging connections that
	 * completed RDMA establishment but didn't start iscsi login
	 * process. So work-around this by cleaning up what ever piled
	 * up in np_accept_list.
	 */
	mutex_lock(&isert_np->np_accept_mutex);
	if (!list_empty(&isert_np->np_accept_list)) {
		pr_info("Still have isert connections, cleaning up...\n");
		list_for_each_entry_safe(isert_conn, n,
					 &isert_np->np_accept_list,
					 conn_accept_node) {
			pr_info("cleaning isert_conn %p state (%d)\n",
				   isert_conn, isert_conn->state);
			isert_connect_release(isert_conn);
		}
	}
	mutex_unlock(&isert_np->np_accept_mutex);

	np->np_context = NULL;
	kfree(isert_np);
}

static void isert_release_work(struct work_struct *work)
{
	struct isert_conn *isert_conn = container_of(work,
						     struct isert_conn,
						     release_work);

	pr_info("Starting release conn %p\n", isert_conn);

	wait_for_completion(&isert_conn->conn_wait);

	mutex_lock(&isert_conn->conn_mutex);
	isert_conn->state = ISER_CONN_DOWN;
	mutex_unlock(&isert_conn->conn_mutex);

	pr_info("Destroying conn %p\n", isert_conn);
	isert_put_conn(isert_conn);
}

static void isert_wait_conn(struct iscsi_conn *conn)
{
	struct isert_conn *isert_conn = conn->context;

	pr_debug("isert_wait_conn: Starting \n");

	mutex_lock(&isert_conn->conn_mutex);
	/*
	 * Only wait for conn_wait_comp_err if the isert_conn made it
	 * into full feature phase..
	 */
	if (isert_conn->state == ISER_CONN_INIT) {
		mutex_unlock(&isert_conn->conn_mutex);
		return;
	}
	isert_conn_terminate(isert_conn);
	mutex_unlock(&isert_conn->conn_mutex);

	wait_for_completion(&isert_conn->conn_wait_comp_err);

	INIT_WORK(&isert_conn->release_work, isert_release_work);
	queue_work(isert_release_wq, &isert_conn->release_work);
}

static void isert_free_conn(struct iscsi_conn *conn)
{
	struct isert_conn *isert_conn = conn->context;

	isert_put_conn(isert_conn);
}

static struct iscsit_transport iser_target_transport = {
	.name			= "IB/iSER",
	.transport_type		= ISCSI_INFINIBAND,
	.priv_size		= sizeof(struct isert_cmd),
	.owner			= THIS_MODULE,
	.iscsit_setup_np	= isert_setup_np,
	.iscsit_accept_np	= isert_accept_np,
	.iscsit_free_np		= isert_free_np,
	.iscsit_wait_conn	= isert_wait_conn,
	.iscsit_free_conn	= isert_free_conn,
	.iscsit_get_login_rx	= isert_get_login_rx,
	.iscsit_put_login_tx	= isert_put_login_tx,
	.iscsit_immediate_queue	= isert_immediate_queue,
	.iscsit_response_queue	= isert_response_queue,
	.iscsit_get_dataout	= isert_get_dataout,
	.iscsit_queue_data_in	= isert_put_datain,
	.iscsit_queue_status	= isert_put_response,
	.iscsit_aborted_task	= isert_aborted_task,
	.iscsit_get_sup_prot_ops = isert_get_sup_prot_ops,
};

static int __init isert_init(void)
{
	int ret;

	isert_rx_wq = alloc_workqueue("isert_rx_wq", 0, 0);
	if (!isert_rx_wq) {
		pr_err("Unable to allocate isert_rx_wq\n");
		return -ENOMEM;
	}

	isert_comp_wq = alloc_workqueue("isert_comp_wq", 0, 0);
	if (!isert_comp_wq) {
		pr_err("Unable to allocate isert_comp_wq\n");
		ret = -ENOMEM;
		goto destroy_rx_wq;
	}

	isert_release_wq = alloc_workqueue("isert_release_wq", WQ_UNBOUND,
					WQ_UNBOUND_MAX_ACTIVE);
	if (!isert_release_wq) {
		pr_err("Unable to allocate isert_release_wq\n");
		ret = -ENOMEM;
		goto destroy_comp_wq;
	}

	iscsit_register_transport(&iser_target_transport);
	pr_info("iSER_TARGET[0] - Loaded iser_target_transport\n");

	return 0;

destroy_comp_wq:
	destroy_workqueue(isert_comp_wq);
destroy_rx_wq:
	destroy_workqueue(isert_rx_wq);
	return ret;
}

static void __exit isert_exit(void)
{
	flush_scheduled_work();
	destroy_workqueue(isert_release_wq);
	destroy_workqueue(isert_comp_wq);
	destroy_workqueue(isert_rx_wq);
	iscsit_unregister_transport(&iser_target_transport);
	pr_debug("iSER_TARGET[0] - Released iser_target_transport\n");
}

MODULE_DESCRIPTION("iSER-Target for mainline target infrastructure");
MODULE_VERSION("0.1");
MODULE_AUTHOR("nab@Linux-iSCSI.org");
MODULE_LICENSE("GPL");

module_init(isert_init);
module_exit(isert_exit);
