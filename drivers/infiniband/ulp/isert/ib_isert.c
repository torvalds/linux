/*******************************************************************************
 * This file contains iSCSI extentions for RDMA (iSER) Verbs
 *
 * (c) Copyright 2013 RisingTide Systems LLC.
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
static struct kmem_cache *isert_cmd_cache;

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
	struct ib_device_attr devattr;
	int ret, index, min_index = 0;

	memset(&devattr, 0, sizeof(struct ib_device_attr));
	ret = isert_query_device(cma_id->device, &devattr);
	if (ret)
		return ret;

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
	 * work-around for RDMA_READ..
	 */
	attr.cap.max_send_sge = devattr.max_sge - 2;
	isert_conn->max_sge = attr.cap.max_send_sge;

	attr.cap.max_recv_sge = 1;
	attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	attr.qp_type = IB_QPT_RC;

	pr_debug("isert_conn_setup_qp cma_id->device: %p\n",
		 cma_id->device);
	pr_debug("isert_conn_setup_qp conn_pd->device: %p\n",
		 isert_conn->conn_pd->device);

	ret = rdma_create_qp(cma_id, isert_conn->conn_pd, &attr);
	if (ret) {
		pr_err("rdma_create_qp failed for cma_id %d\n", ret);
		return ret;
	}
	isert_conn->conn_qp = cma_id->qp;
	pr_debug("rdma_create_qp() returned success >>>>>>>>>>>>>>>>>>>>>>>>>.\n");

	return 0;
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

static void isert_cq_tx_callback(struct ib_cq *, void *);
static void isert_cq_rx_callback(struct ib_cq *, void *);

static int
isert_create_device_ib_res(struct isert_device *device)
{
	struct ib_device *ib_dev = device->ib_device;
	struct isert_cq_desc *cq_desc;
	int ret = 0, i, j;

	device->cqs_used = min_t(int, num_online_cpus(),
				 device->ib_device->num_comp_vectors);
	device->cqs_used = min(ISERT_MAX_CQ, device->cqs_used);
	pr_debug("Using %d CQs, device %s supports %d vectors\n",
		 device->cqs_used, device->ib_device->name,
		 device->ib_device->num_comp_vectors);
	device->cq_desc = kzalloc(sizeof(struct isert_cq_desc) *
				device->cqs_used, GFP_KERNEL);
	if (!device->cq_desc) {
		pr_err("Unable to allocate device->cq_desc\n");
		return -ENOMEM;
	}
	cq_desc = device->cq_desc;

	device->dev_pd = ib_alloc_pd(ib_dev);
	if (IS_ERR(device->dev_pd)) {
		ret = PTR_ERR(device->dev_pd);
		pr_err("ib_alloc_pd failed for dev_pd: %d\n", ret);
		goto out_cq_desc;
	}

	for (i = 0; i < device->cqs_used; i++) {
		cq_desc[i].device = device;
		cq_desc[i].cq_index = i;

		device->dev_rx_cq[i] = ib_create_cq(device->ib_device,
						isert_cq_rx_callback,
						isert_cq_event_callback,
						(void *)&cq_desc[i],
						ISER_MAX_RX_CQ_LEN, i);
		if (IS_ERR(device->dev_rx_cq[i])) {
			ret = PTR_ERR(device->dev_rx_cq[i]);
			device->dev_rx_cq[i] = NULL;
			goto out_cq;
		}

		device->dev_tx_cq[i] = ib_create_cq(device->ib_device,
						isert_cq_tx_callback,
						isert_cq_event_callback,
						(void *)&cq_desc[i],
						ISER_MAX_TX_CQ_LEN, i);
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

	device->dev_mr = ib_get_dma_mr(device->dev_pd, IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(device->dev_mr)) {
		ret = PTR_ERR(device->dev_mr);
		pr_err("ib_get_dma_mr failed for dev_mr: %d\n", ret);
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
	ib_dealloc_pd(device->dev_pd);

out_cq_desc:
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

	ib_dereg_mr(device->dev_mr);
	ib_dealloc_pd(device->dev_pd);
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

static int
isert_connect_request(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	struct iscsi_np *np = cma_id->context;
	struct isert_np *isert_np = np->np_context;
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
	init_completion(&isert_conn->conn_wait);
	init_completion(&isert_conn->conn_wait_comp_err);
	kref_init(&isert_conn->conn_kref);
	kref_get(&isert_conn->conn_kref);
	mutex_init(&isert_conn->conn_mutex);

	cma_id->context = isert_conn;
	isert_conn->conn_cm_id = cma_id;
	isert_conn->responder_resources = event->param.conn.responder_resources;
	isert_conn->initiator_depth = event->param.conn.initiator_depth;
	pr_debug("Using responder_resources: %u initiator_depth: %u\n",
		 isert_conn->responder_resources, isert_conn->initiator_depth);

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

	isert_conn->conn_device = device;
	isert_conn->conn_pd = device->dev_pd;
	isert_conn->conn_mr = device->dev_mr;

	ret = isert_conn_setup_qp(isert_conn, cma_id);
	if (ret)
		goto out_conn_dev;

	mutex_lock(&isert_np->np_accept_mutex);
	list_add_tail(&isert_conn->conn_accept_node, &isert_np->np_accept_list);
	mutex_unlock(&isert_np->np_accept_mutex);

	pr_debug("isert_connect_request() up np_sem np: %p\n", np);
	up(&isert_np->np_sem);
	return 0;

out_conn_dev:
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
	return ret;
}

static void
isert_connect_release(struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct isert_device *device = isert_conn->conn_device;
	int cq_index;

	pr_debug("Entering isert_connect_release(): >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

	if (isert_conn->conn_qp) {
		cq_index = ((struct isert_cq_desc *)
			isert_conn->conn_qp->recv_cq->cq_context)->cq_index;
		pr_debug("isert_connect_release: cq_index: %d\n", cq_index);
		isert_conn->conn_device->cq_active_qps[cq_index]--;

		rdma_destroy_qp(isert_conn->conn_cm_id);
	}

	isert_free_rx_descriptors(isert_conn);
	rdma_destroy_id(isert_conn->conn_cm_id);

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
	return;
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

static void
isert_disconnect_work(struct work_struct *work)
{
	struct isert_conn *isert_conn = container_of(work,
				struct isert_conn, conn_logout_work);

	pr_debug("isert_disconnect_work(): >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	mutex_lock(&isert_conn->conn_mutex);
	if (isert_conn->state == ISER_CONN_UP)
		isert_conn->state = ISER_CONN_TERMINATING;

	if (isert_conn->post_recv_buf_count == 0 &&
	    atomic_read(&isert_conn->post_send_buf_count) == 0) {
		mutex_unlock(&isert_conn->conn_mutex);
		goto wake_up;
	}
	if (!isert_conn->conn_cm_id) {
		mutex_unlock(&isert_conn->conn_mutex);
		isert_put_conn(isert_conn);
		return;
	}

	if (isert_conn->disconnect) {
		/* Send DREQ/DREP towards our initiator */
		rdma_disconnect(isert_conn->conn_cm_id);
	}

	mutex_unlock(&isert_conn->conn_mutex);

wake_up:
	complete(&isert_conn->conn_wait);
	isert_put_conn(isert_conn);
}

static void
isert_disconnected_handler(struct rdma_cm_id *cma_id, bool disconnect)
{
	struct isert_conn *isert_conn = (struct isert_conn *)cma_id->context;

	isert_conn->disconnect = disconnect;
	INIT_WORK(&isert_conn->conn_logout_work, isert_disconnect_work);
	schedule_work(&isert_conn->conn_logout_work);
}

static int
isert_cma_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	int ret = 0;
	bool disconnect = false;

	pr_debug("isert_cma_handler: event %d status %d conn %p id %p\n",
		 event->event, event->status, cma_id->context, cma_id);

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		ret = isert_connect_request(cma_id, event);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		isert_connected_handler(cma_id);
		break;
	case RDMA_CM_EVENT_ADDR_CHANGE:    /* FALLTHRU */
	case RDMA_CM_EVENT_DISCONNECTED:   /* FALLTHRU */
	case RDMA_CM_EVENT_DEVICE_REMOVAL: /* FALLTHRU */
		disconnect = true;
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:  /* FALLTHRU */
		isert_disconnected_handler(cma_id, disconnect);
		break;
	case RDMA_CM_EVENT_CONNECT_ERROR:
	default:
		pr_err("Unhandled RDMA CMA event: %d\n", event->event);
		break;
	}

	if (ret != 0) {
		pr_err("isert_cma_handler failed RDMA_CM_EVENT: 0x%08x %d\n",
		       event->event, ret);
		dump_stack();
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
		rx_wr->wr_id	= (unsigned long)rx_desc;
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
	send_wr.wr_id	= (unsigned long)tx_desc;
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
isert_init_send_wr(struct isert_cmd *isert_cmd, struct ib_send_wr *send_wr)
{
	isert_cmd->rdma_wr.iser_ib_op = ISER_IB_SEND;
	send_wr->wr_id = (unsigned long)&isert_cmd->tx_desc;
	send_wr->opcode = IB_WR_SEND;
	send_wr->send_flags = IB_SEND_SIGNALED;
	send_wr->sg_list = &isert_cmd->tx_desc.tx_sg[0];
	send_wr->num_sge = isert_cmd->tx_desc.num_sge;
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
	rx_wr.wr_id = (unsigned long)isert_conn->login_req_buf;
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
			ret = isert_alloc_rx_descriptors(isert_conn);
			if (ret)
				return ret;

			ret = isert_post_recv(isert_conn, ISERT_MIN_POSTED_RX);
			if (ret)
				return ret;

			isert_conn->state = ISER_CONN_UP;
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
isert_rx_login_req(struct iser_rx_desc *rx_desc, int rx_buflen,
		   struct isert_conn *isert_conn)
{
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_login *login = conn->conn_login;
	int size;

	if (!login) {
		pr_err("conn->conn_login is NULL\n");
		dump_stack();
		return;
	}

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

	complete(&isert_conn->conn_login_comp);
}

static void
isert_release_cmd(struct iscsi_cmd *cmd)
{
	struct isert_cmd *isert_cmd = container_of(cmd, struct isert_cmd,
						   iscsi_cmd);

	pr_debug("Entering isert_release_cmd %p >>>>>>>>>>>>>>>.\n", isert_cmd);

	kfree(cmd->buf_ptr);
	kfree(cmd->tmr_req);

	kmem_cache_free(isert_cmd_cache, isert_cmd);
}

static struct iscsi_cmd
*isert_alloc_cmd(struct iscsi_conn *conn, gfp_t gfp)
{
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct isert_cmd *isert_cmd;

	isert_cmd = kmem_cache_zalloc(isert_cmd_cache, gfp);
	if (!isert_cmd) {
		pr_err("Unable to allocate isert_cmd\n");
		return NULL;
	}
	isert_cmd->conn = isert_conn;
	isert_cmd->iscsi_cmd.release_cmd = &isert_release_cmd;

	return &isert_cmd->iscsi_cmd;
}

static int
isert_handle_scsi_cmd(struct isert_conn *isert_conn,
		      struct isert_cmd *isert_cmd, struct iser_rx_desc *rx_desc,
		      unsigned char *buf)
{
	struct iscsi_cmd *cmd = &isert_cmd->iscsi_cmd;
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
isert_rx_opcode(struct isert_conn *isert_conn, struct iser_rx_desc *rx_desc,
		uint32_t read_stag, uint64_t read_va,
		uint32_t write_stag, uint64_t write_va)
{
	struct iscsi_hdr *hdr = &rx_desc->iscsi_header;
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_cmd *cmd;
	struct isert_cmd *isert_cmd;
	int ret = -EINVAL;
	u8 opcode = (hdr->opcode & ISCSI_OPCODE_MASK);

	switch (opcode) {
	case ISCSI_OP_SCSI_CMD:
		cmd = iscsit_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			break;

		isert_cmd = container_of(cmd, struct isert_cmd, iscsi_cmd);
		isert_cmd->read_stag = read_stag;
		isert_cmd->read_va = read_va;
		isert_cmd->write_stag = write_stag;
		isert_cmd->write_va = write_va;

		ret = isert_handle_scsi_cmd(isert_conn, isert_cmd,
					rx_desc, (unsigned char *)hdr);
		break;
	case ISCSI_OP_NOOP_OUT:
		cmd = iscsit_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			break;

		ret = iscsit_handle_nop_out(conn, cmd, (unsigned char *)hdr);
		break;
	case ISCSI_OP_SCSI_DATA_OUT:
		ret = isert_handle_iscsi_dataout(isert_conn, rx_desc,
						(unsigned char *)hdr);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		cmd = iscsit_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			break;

		ret = iscsit_handle_task_mgt_cmd(conn, cmd,
						(unsigned char *)hdr);
		break;
	case ISCSI_OP_LOGOUT:
		cmd = iscsit_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			break;

		ret = iscsit_handle_logout_cmd(conn, cmd, (unsigned char *)hdr);
		if (ret > 0)
			wait_for_completion_timeout(&conn->conn_logout_comp,
						    SECONDS_FOR_LOGOUT_COMP *
						    HZ);
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

	if ((char *)desc == isert_conn->login_req_buf)
		isert_rx_login_req(desc, xfer_len - ISER_HEADERS_LEN,
				   isert_conn);
	else
		isert_rx_do_work(desc, isert_conn);

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

static void
isert_unmap_cmd(struct isert_cmd *isert_cmd, struct isert_conn *isert_conn)
{
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;

	pr_debug("isert_unmap_cmd >>>>>>>>>>>>>>>>>>>>>>>\n");

	if (wr->sge) {
		ib_dma_unmap_sg(ib_dev, wr->sge, wr->num_sge, DMA_TO_DEVICE);
		wr->sge = NULL;
	}

	kfree(wr->send_wr);
	wr->send_wr = NULL;

	kfree(isert_cmd->ib_sge);
	isert_cmd->ib_sge = NULL;
}

static void
isert_put_cmd(struct isert_cmd *isert_cmd, bool comp_err)
{
	struct iscsi_cmd *cmd = &isert_cmd->iscsi_cmd;
	struct isert_conn *isert_conn = isert_cmd->conn;
	struct iscsi_conn *conn = isert_conn->conn;

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

		isert_unmap_cmd(isert_cmd, isert_conn);
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
		isert_release_cmd(cmd);
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
	if (isert_cmd->sense_buf_dma != 0) {
		pr_debug("Calling ib_dma_unmap_single for isert_cmd->sense_buf_dma\n");
		ib_dma_unmap_single(ib_dev, isert_cmd->sense_buf_dma,
				    isert_cmd->sense_buf_len, DMA_TO_DEVICE);
		isert_cmd->sense_buf_dma = 0;
	}

	isert_unmap_tx_desc(tx_desc, ib_dev);
	isert_put_cmd(isert_cmd, comp_err);
}

static void
isert_completion_rdma_read(struct iser_tx_desc *tx_desc,
			   struct isert_cmd *isert_cmd)
{
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct iscsi_cmd *cmd = &isert_cmd->iscsi_cmd;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct ib_device *ib_dev = isert_cmd->conn->conn_cm_id->device;

	iscsit_stop_dataout_timer(cmd);

	if (wr->sge) {
		pr_debug("isert_do_rdma_read_comp: Unmapping wr->sge from t_data_sg\n");
		ib_dma_unmap_sg(ib_dev, wr->sge, wr->num_sge, DMA_TO_DEVICE);
		wr->sge = NULL;
	}

	if (isert_cmd->ib_sge) {
		pr_debug("isert_do_rdma_read_comp: Freeing isert_cmd->ib_sge\n");
		kfree(isert_cmd->ib_sge);
		isert_cmd->ib_sge = NULL;
	}

	cmd->write_data_done = se_cmd->data_length;
	wr->send_wr_num = 0;

	pr_debug("isert_do_rdma_read_comp, calling target_execute_cmd\n");
	spin_lock_bh(&cmd->istate_lock);
	cmd->cmd_flags |= ICF_GOT_LAST_DATAOUT;
	cmd->i_state = ISTATE_RECEIVED_LAST_DATAOUT;
	spin_unlock_bh(&cmd->istate_lock);

	target_execute_cmd(se_cmd);
}

static void
isert_do_control_comp(struct work_struct *work)
{
	struct isert_cmd *isert_cmd = container_of(work,
			struct isert_cmd, comp_work);
	struct isert_conn *isert_conn = isert_cmd->conn;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iscsi_cmd *cmd = &isert_cmd->iscsi_cmd;

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
	struct iscsi_cmd *cmd = &isert_cmd->iscsi_cmd;
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;

	if (cmd->i_state == ISTATE_SEND_TASKMGTRSP ||
	    cmd->i_state == ISTATE_SEND_LOGOUTRSP ||
	    cmd->i_state == ISTATE_SEND_REJECT) {
		isert_unmap_tx_desc(tx_desc, ib_dev);

		INIT_WORK(&isert_cmd->comp_work, isert_do_control_comp);
		queue_work(isert_comp_wq, &isert_cmd->comp_work);
		return;
	}
	atomic_sub(wr->send_wr_num + 1, &isert_conn->post_send_buf_count);

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
		pr_err("isert_send_completion: Got ISER_IB_RDMA_WRITE\n");
		dump_stack();
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
isert_cq_tx_comp_err(struct iser_tx_desc *tx_desc, struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct isert_cmd *isert_cmd = tx_desc->isert_cmd;

	if (!isert_cmd)
		isert_unmap_tx_desc(tx_desc, ib_dev);
	else
		isert_completion_put(tx_desc, isert_cmd, ib_dev, true);
}

static void
isert_cq_rx_comp_err(struct isert_conn *isert_conn)
{
	struct iscsi_conn *conn = isert_conn->conn;

	if (isert_conn->post_recv_buf_count)
		return;

	if (conn->sess) {
		target_sess_cmd_list_set_waiting(conn->sess->se_sess);
		target_wait_for_sess_cmds(conn->sess->se_sess);
	}

	while (atomic_read(&isert_conn->post_send_buf_count))
		msleep(3000);

	mutex_lock(&isert_conn->conn_mutex);
	isert_conn->state = ISER_CONN_DOWN;
	mutex_unlock(&isert_conn->conn_mutex);

	iscsit_cause_connection_reinstatement(isert_conn->conn, 0);

	complete(&isert_conn->conn_wait_comp_err);
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
		tx_desc = (struct iser_tx_desc *)(unsigned long)wc.wr_id;
		isert_conn = wc.qp->qp_context;

		if (wc.status == IB_WC_SUCCESS) {
			isert_send_completion(tx_desc, isert_conn);
		} else {
			pr_debug("TX wc.status != IB_WC_SUCCESS >>>>>>>>>>>>>>\n");
			pr_debug("TX wc.status: 0x%08x\n", wc.status);
			atomic_dec(&isert_conn->post_send_buf_count);
			isert_cq_tx_comp_err(tx_desc, isert_conn);
		}
	}

	ib_req_notify_cq(tx_cq, IB_CQ_NEXT_COMP);
}

static void
isert_cq_tx_callback(struct ib_cq *cq, void *context)
{
	struct isert_cq_desc *cq_desc = (struct isert_cq_desc *)context;

	INIT_WORK(&cq_desc->cq_tx_work, isert_cq_tx_work);
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
		rx_desc = (struct iser_rx_desc *)(unsigned long)wc.wr_id;
		isert_conn = wc.qp->qp_context;

		if (wc.status == IB_WC_SUCCESS) {
			xfer_len = (unsigned long)wc.byte_len;
			isert_rx_completion(rx_desc, isert_conn, xfer_len);
		} else {
			pr_debug("RX wc.status != IB_WC_SUCCESS >>>>>>>>>>>>>>\n");
			if (wc.status != IB_WC_WR_FLUSH_ERR)
				pr_debug("RX wc.status: 0x%08x\n", wc.status);

			isert_conn->post_recv_buf_count--;
			isert_cq_rx_comp_err(isert_conn);
		}
	}

	ib_req_notify_cq(rx_cq, IB_CQ_NEXT_COMP);
}

static void
isert_cq_rx_callback(struct ib_cq *cq, void *context)
{
	struct isert_cq_desc *cq_desc = (struct isert_cq_desc *)context;

	INIT_WORK(&cq_desc->cq_rx_work, isert_cq_rx_work);
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
	struct isert_cmd *isert_cmd = container_of(cmd,
					struct isert_cmd, iscsi_cmd);
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
		u32 padding, sense_len;

		put_unaligned_be16(cmd->se_cmd.scsi_sense_length,
				   cmd->sense_buffer);
		cmd->se_cmd.scsi_sense_length += sizeof(__be16);

		padding = -(cmd->se_cmd.scsi_sense_length) & 3;
		hton24(hdr->dlength, (u32)cmd->se_cmd.scsi_sense_length);
		sense_len = cmd->se_cmd.scsi_sense_length + padding;

		isert_cmd->sense_buf_dma = ib_dma_map_single(ib_dev,
				(void *)cmd->sense_buffer, sense_len,
				DMA_TO_DEVICE);

		isert_cmd->sense_buf_len = sense_len;
		tx_dsg->addr	= isert_cmd->sense_buf_dma;
		tx_dsg->length	= sense_len;
		tx_dsg->lkey	= isert_conn->conn_mr->lkey;
		isert_cmd->tx_desc.num_sge = 2;
	}

	isert_init_send_wr(isert_cmd, send_wr);

	pr_debug("Posting SCSI Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_nopin(struct iscsi_cmd *cmd, struct iscsi_conn *conn,
		bool nopout_response)
{
	struct isert_cmd *isert_cmd = container_of(cmd,
				struct isert_cmd, iscsi_cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_nopin_rsp(cmd, conn, (struct iscsi_nopin *)
			       &isert_cmd->tx_desc.iscsi_header,
			       nopout_response);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_cmd, send_wr);

	pr_debug("Posting NOPIN Reponse IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_logout_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = container_of(cmd,
				struct isert_cmd, iscsi_cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_logout_rsp(cmd, conn, (struct iscsi_logout_rsp *)
				&isert_cmd->tx_desc.iscsi_header);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_cmd, send_wr);

	pr_debug("Posting Logout Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_tm_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = container_of(cmd,
				struct isert_cmd, iscsi_cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_task_mgt_rsp(cmd, conn, (struct iscsi_tm_rsp *)
				  &isert_cmd->tx_desc.iscsi_header);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_cmd, send_wr);

	pr_debug("Posting Task Management Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_reject(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = container_of(cmd,
				struct isert_cmd, iscsi_cmd);
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
	isert_cmd->sense_buf_dma = ib_dma_map_single(ib_dev,
			(void *)cmd->buf_ptr, ISCSI_HDR_LEN,
			DMA_TO_DEVICE);
	isert_cmd->sense_buf_len = ISCSI_HDR_LEN;
	tx_dsg->addr	= isert_cmd->sense_buf_dma;
	tx_dsg->length	= ISCSI_HDR_LEN;
	tx_dsg->lkey	= isert_conn->conn_mr->lkey;
	isert_cmd->tx_desc.num_sge = 2;

	isert_init_send_wr(isert_cmd, send_wr);

	pr_debug("Posting Reject IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_build_rdma_wr(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd,
		    struct ib_sge *ib_sge, struct ib_send_wr *send_wr,
		    u32 data_left, u32 offset)
{
	struct iscsi_cmd *cmd = &isert_cmd->iscsi_cmd;
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
	send_wr->wr_id = (unsigned long)&isert_cmd->tx_desc;
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

		pr_debug("RDMA ib_sge: addr: 0x%16llx  length: %u\n",
			 ib_sge->addr, ib_sge->length);
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
isert_put_datain(struct iscsi_conn *conn, struct iscsi_cmd *cmd)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_cmd *isert_cmd = container_of(cmd,
					struct isert_cmd, iscsi_cmd);
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *wr_failed, *send_wr;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct ib_sge *ib_sge;
	struct scatterlist *sg;
	u32 offset = 0, data_len, data_left, rdma_write_max;
	int rc, ret = 0, count, sg_nents, i, ib_sge_cnt;

	pr_debug("RDMA_WRITE: data_length: %u\n", se_cmd->data_length);

	sg = &se_cmd->t_data_sg[0];
	sg_nents = se_cmd->t_data_nents;

	count = ib_dma_map_sg(ib_dev, sg, sg_nents, DMA_TO_DEVICE);
	if (unlikely(!count)) {
		pr_err("Unable to map put_datain SGs\n");
		return -EINVAL;
	}
	wr->sge = sg;
	wr->num_sge = sg_nents;
	pr_debug("Mapped IB count: %u sg: %p sg_nents: %u for RDMA_WRITE\n",
		 count, sg, sg_nents);

	ib_sge = kzalloc(sizeof(struct ib_sge) * sg_nents, GFP_KERNEL);
	if (!ib_sge) {
		pr_warn("Unable to allocate datain ib_sge\n");
		ret = -ENOMEM;
		goto unmap_sg;
	}
	isert_cmd->ib_sge = ib_sge;

	pr_debug("Allocated ib_sge: %p from t_data_ents: %d for RDMA_WRITE\n",
		 ib_sge, se_cmd->t_data_nents);

	wr->send_wr_num = DIV_ROUND_UP(sg_nents, isert_conn->max_sge);
	wr->send_wr = kzalloc(sizeof(struct ib_send_wr) * wr->send_wr_num,
				GFP_KERNEL);
	if (!wr->send_wr) {
		pr_err("Unable to allocate wr->send_wr\n");
		ret = -ENOMEM;
		goto unmap_sg;
	}
	pr_debug("Allocated wr->send_wr: %p wr->send_wr_num: %u\n",
		 wr->send_wr, wr->send_wr_num);

	iscsit_increment_maxcmdsn(cmd, conn->sess);
	cmd->stat_sn = conn->stat_sn++;

	wr->isert_cmd = isert_cmd;
	rdma_write_max = isert_conn->max_sge * PAGE_SIZE;
	data_left = se_cmd->data_length;

	for (i = 0; i < wr->send_wr_num; i++) {
		send_wr = &isert_cmd->rdma_wr.send_wr[i];
		data_len = min(data_left, rdma_write_max);

		send_wr->opcode = IB_WR_RDMA_WRITE;
		send_wr->send_flags = 0;
		send_wr->wr.rdma.remote_addr = isert_cmd->read_va + offset;
		send_wr->wr.rdma.rkey = isert_cmd->read_stag;

		ib_sge_cnt = isert_build_rdma_wr(isert_conn, isert_cmd, ib_sge,
					send_wr, data_len, offset);
		ib_sge += ib_sge_cnt;

		if (i + 1 == wr->send_wr_num)
			send_wr->next = &isert_cmd->tx_desc.send_wr;
		else
			send_wr->next = &wr->send_wr[i + 1];

		offset += data_len;
		data_left -= data_len;
	}
	/*
	 * Build isert_conn->tx_desc for iSCSI response PDU and attach
	 */
	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_rsp_pdu(cmd, conn, false, (struct iscsi_scsi_rsp *)
			     &isert_cmd->tx_desc.iscsi_header);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_cmd, &isert_cmd->tx_desc.send_wr);

	atomic_add(wr->send_wr_num + 1, &isert_conn->post_send_buf_count);

	rc = ib_post_send(isert_conn->conn_qp, wr->send_wr, &wr_failed);
	if (rc) {
		pr_warn("ib_post_send() failed for IB_WR_RDMA_WRITE\n");
		atomic_sub(wr->send_wr_num + 1, &isert_conn->post_send_buf_count);
	}
	pr_debug("Posted RDMA_WRITE + Response for iSER Data READ\n");
	return 1;

unmap_sg:
	ib_dma_unmap_sg(ib_dev, sg, sg_nents, DMA_TO_DEVICE);
	return ret;
}

static int
isert_get_dataout(struct iscsi_conn *conn, struct iscsi_cmd *cmd, bool recovery)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_cmd *isert_cmd = container_of(cmd,
					struct isert_cmd, iscsi_cmd);
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *wr_failed, *send_wr;
	struct ib_sge *ib_sge;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct scatterlist *sg_start;
	u32 sg_off, sg_nents, page_off, va_offset = 0;
	u32 offset = 0, data_len, data_left, rdma_write_max;
	int rc, ret = 0, count, i, ib_sge_cnt;

	pr_debug("RDMA_READ: data_length: %u write_data_done: %u\n",
		 se_cmd->data_length, cmd->write_data_done);

	sg_off = cmd->write_data_done / PAGE_SIZE;
	sg_start = &cmd->se_cmd.t_data_sg[sg_off];
	page_off = cmd->write_data_done % PAGE_SIZE;

	pr_debug("RDMA_READ: sg_off: %d, sg_start: %p page_off: %d\n",
		 sg_off, sg_start, page_off);

	data_left = se_cmd->data_length - cmd->write_data_done;
	sg_nents = se_cmd->t_data_nents - sg_off;

	pr_debug("RDMA_READ: data_left: %d, sg_nents: %d\n",
		 data_left, sg_nents);

	count = ib_dma_map_sg(ib_dev, sg_start, sg_nents, DMA_FROM_DEVICE);
	if (unlikely(!count)) {
		pr_err("Unable to map get_dataout SGs\n");
		return -EINVAL;
	}
	wr->sge = sg_start;
	wr->num_sge = sg_nents;
	pr_debug("Mapped IB count: %u sg_start: %p sg_nents: %u for RDMA_READ\n",
		 count, sg_start, sg_nents);

	ib_sge = kzalloc(sizeof(struct ib_sge) * sg_nents, GFP_KERNEL);
	if (!ib_sge) {
		pr_warn("Unable to allocate dataout ib_sge\n");
		ret = -ENOMEM;
		goto unmap_sg;
	}
	isert_cmd->ib_sge = ib_sge;

	pr_debug("Using ib_sge: %p from sg_ents: %d for RDMA_READ\n",
		 ib_sge, sg_nents);

	wr->send_wr_num = DIV_ROUND_UP(sg_nents, isert_conn->max_sge);
	wr->send_wr = kzalloc(sizeof(struct ib_send_wr) * wr->send_wr_num,
				GFP_KERNEL);
	if (!wr->send_wr) {
		pr_debug("Unable to allocate wr->send_wr\n");
		ret = -ENOMEM;
		goto unmap_sg;
	}
	pr_debug("Allocated wr->send_wr: %p wr->send_wr_num: %u\n",
		 wr->send_wr, wr->send_wr_num);

	isert_cmd->tx_desc.isert_cmd = isert_cmd;

	wr->iser_ib_op = ISER_IB_RDMA_READ;
	wr->isert_cmd = isert_cmd;
	rdma_write_max = isert_conn->max_sge * PAGE_SIZE;
	offset = cmd->write_data_done;

	for (i = 0; i < wr->send_wr_num; i++) {
		send_wr = &isert_cmd->rdma_wr.send_wr[i];
		data_len = min(data_left, rdma_write_max);

		send_wr->opcode = IB_WR_RDMA_READ;
		send_wr->wr.rdma.remote_addr = isert_cmd->write_va + va_offset;
		send_wr->wr.rdma.rkey = isert_cmd->write_stag;

		ib_sge_cnt = isert_build_rdma_wr(isert_conn, isert_cmd, ib_sge,
					send_wr, data_len, offset);
		ib_sge += ib_sge_cnt;

		if (i + 1 == wr->send_wr_num)
			send_wr->send_flags = IB_SEND_SIGNALED;
		else
			send_wr->next = &wr->send_wr[i + 1];

		offset += data_len;
		va_offset += data_len;
		data_left -= data_len;
	}

	atomic_add(wr->send_wr_num, &isert_conn->post_send_buf_count);

	rc = ib_post_send(isert_conn->conn_qp, wr->send_wr, &wr_failed);
	if (rc) {
		pr_warn("ib_post_send() failed for IB_WR_RDMA_READ\n");
		atomic_sub(wr->send_wr_num, &isert_conn->post_send_buf_count);
	}
	pr_debug("Posted RDMA_READ memory for ISER Data WRITE\n");
	return 0;

unmap_sg:
	ib_dma_unmap_sg(ib_dev, sg_start, sg_nents, DMA_FROM_DEVICE);
	return ret;
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

static int
isert_setup_np(struct iscsi_np *np,
	       struct __kernel_sockaddr_storage *ksockaddr)
{
	struct isert_np *isert_np;
	struct rdma_cm_id *isert_lid;
	struct sockaddr *sa;
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

	sa = (struct sockaddr *)ksockaddr;
	pr_debug("ksockaddr: %p, sa: %p\n", ksockaddr, sa);
	/*
	 * Setup the np->np_sockaddr from the passed sockaddr setup
	 * in iscsi_target_configfs.c code..
	 */
	memcpy(&np->np_sockaddr, ksockaddr,
	       sizeof(struct __kernel_sockaddr_storage));

	isert_lid = rdma_create_id(isert_cma_handler, np, RDMA_PS_TCP,
				IB_QPT_RC);
	if (IS_ERR(isert_lid)) {
		pr_err("rdma_create_id() for isert_listen_handler failed: %ld\n",
		       PTR_ERR(isert_lid));
		ret = PTR_ERR(isert_lid);
		goto out;
	}

	ret = rdma_bind_addr(isert_lid, sa);
	if (ret) {
		pr_err("rdma_bind_addr() for isert_lid failed: %d\n", ret);
		goto out_lid;
	}

	ret = rdma_listen(isert_lid, ISERT_RDMA_LISTEN_BACKLOG);
	if (ret) {
		pr_err("rdma_listen() for isert_lid failed: %d\n", ret);
		goto out_lid;
	}

	isert_np->np_cm_id = isert_lid;
	np->np_context = isert_np;
	pr_debug("Setup isert_lid->context: %p\n", isert_lid->context);

	return 0;

out_lid:
	rdma_destroy_id(isert_lid);
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
	cp.responder_resources = isert_conn->responder_resources;
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

	pr_debug("isert_get_login_rx before conn_login_comp conn: %p\n", conn);

	ret = wait_for_completion_interruptible(&isert_conn->conn_login_comp);
	if (ret)
		return ret;

	pr_debug("isert_get_login_rx processing login->req: %p\n", login->req);
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
	if (max_accept > 5)
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

	ret = isert_rdma_post_recvl(isert_conn);
	if (ret)
		return ret;

	ret = isert_rdma_accept(isert_conn);
	if (ret)
		return ret;

	isert_set_conn_info(np, conn, isert_conn);

	pr_debug("Processing isert_accept_np: isert_conn: %p\n", isert_conn);
	return 0;
}

static void
isert_free_np(struct iscsi_np *np)
{
	struct isert_np *isert_np = (struct isert_np *)np->np_context;

	rdma_destroy_id(isert_np->np_cm_id);

	np->np_context = NULL;
	kfree(isert_np);
}

static void isert_wait_conn(struct iscsi_conn *conn)
{
	struct isert_conn *isert_conn = conn->context;

	pr_debug("isert_wait_conn: Starting \n");

	mutex_lock(&isert_conn->conn_mutex);
	if (isert_conn->conn_cm_id) {
		pr_debug("Calling rdma_disconnect from isert_wait_conn\n");
		rdma_disconnect(isert_conn->conn_cm_id);
	}
	/*
	 * Only wait for conn_wait_comp_err if the isert_conn made it
	 * into full feature phase..
	 */
	if (isert_conn->state == ISER_CONN_INIT) {
		mutex_unlock(&isert_conn->conn_mutex);
		return;
	}
	if (isert_conn->state == ISER_CONN_UP)
		isert_conn->state = ISER_CONN_TERMINATING;
	mutex_unlock(&isert_conn->conn_mutex);

	wait_for_completion(&isert_conn->conn_wait_comp_err);

	wait_for_completion(&isert_conn->conn_wait);
}

static void isert_free_conn(struct iscsi_conn *conn)
{
	struct isert_conn *isert_conn = conn->context;

	isert_put_conn(isert_conn);
}

static struct iscsit_transport iser_target_transport = {
	.name			= "IB/iSER",
	.transport_type		= ISCSI_INFINIBAND,
	.owner			= THIS_MODULE,
	.iscsit_setup_np	= isert_setup_np,
	.iscsit_accept_np	= isert_accept_np,
	.iscsit_free_np		= isert_free_np,
	.iscsit_wait_conn	= isert_wait_conn,
	.iscsit_free_conn	= isert_free_conn,
	.iscsit_alloc_cmd	= isert_alloc_cmd,
	.iscsit_get_login_rx	= isert_get_login_rx,
	.iscsit_put_login_tx	= isert_put_login_tx,
	.iscsit_immediate_queue	= isert_immediate_queue,
	.iscsit_response_queue	= isert_response_queue,
	.iscsit_get_dataout	= isert_get_dataout,
	.iscsit_queue_data_in	= isert_put_datain,
	.iscsit_queue_status	= isert_put_response,
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

	isert_cmd_cache = kmem_cache_create("isert_cmd_cache",
			sizeof(struct isert_cmd), __alignof__(struct isert_cmd),
			0, NULL);
	if (!isert_cmd_cache) {
		pr_err("Unable to create isert_cmd_cache\n");
		ret = -ENOMEM;
		goto destroy_tx_cq;
	}

	iscsit_register_transport(&iser_target_transport);
	pr_debug("iSER_TARGET[0] - Loaded iser_target_transport\n");
	return 0;

destroy_tx_cq:
	destroy_workqueue(isert_comp_wq);
destroy_rx_wq:
	destroy_workqueue(isert_rx_wq);
	return ret;
}

static void __exit isert_exit(void)
{
	flush_scheduled_work();
	kmem_cache_destroy(isert_cmd_cache);
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
