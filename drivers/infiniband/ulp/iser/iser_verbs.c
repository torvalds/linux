/*
 * Copyright (c) 2004, 2005, 2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
 * Copyright (c) 2013-2014 Mellanox Technologies. All rights reserved.
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
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "iscsi_iser.h"

#define ISCSI_ISER_MAX_CONN	8
#define ISER_MAX_RX_CQ_LEN	(ISER_QP_MAX_RECV_DTOS * ISCSI_ISER_MAX_CONN)
#define ISER_MAX_TX_CQ_LEN	(ISER_QP_MAX_REQ_DTOS  * ISCSI_ISER_MAX_CONN)

static void iser_cq_tasklet_fn(unsigned long data);
static void iser_cq_callback(struct ib_cq *cq, void *cq_context);

static void iser_cq_event_callback(struct ib_event *cause, void *context)
{
	iser_err("got cq event %d \n", cause->event);
}

static void iser_qp_event_callback(struct ib_event *cause, void *context)
{
	iser_err("got qp event %d\n",cause->event);
}

static void iser_event_handler(struct ib_event_handler *handler,
				struct ib_event *event)
{
	iser_err("async event %d on device %s port %d\n", event->event,
		event->device->name, event->element.port_num);
}

/**
 * iser_create_device_ib_res - creates Protection Domain (PD), Completion
 * Queue (CQ), DMA Memory Region (DMA MR) with the device associated with
 * the adapator.
 *
 * returns 0 on success, -1 on failure
 */
static int iser_create_device_ib_res(struct iser_device *device)
{
	struct iser_cq_desc *cq_desc;
	struct ib_device_attr *dev_attr = &device->dev_attr;
	int ret, i, j;

	ret = ib_query_device(device->ib_device, dev_attr);
	if (ret) {
		pr_warn("Query device failed for %s\n", device->ib_device->name);
		return ret;
	}

	/* Assign function handles  - based on FMR support */
	if (device->ib_device->alloc_fmr && device->ib_device->dealloc_fmr &&
	    device->ib_device->map_phys_fmr && device->ib_device->unmap_fmr) {
		iser_info("FMR supported, using FMR for registration\n");
		device->iser_alloc_rdma_reg_res = iser_create_fmr_pool;
		device->iser_free_rdma_reg_res = iser_free_fmr_pool;
		device->iser_reg_rdma_mem = iser_reg_rdma_mem_fmr;
		device->iser_unreg_rdma_mem = iser_unreg_mem_fmr;
	} else
	if (dev_attr->device_cap_flags & IB_DEVICE_MEM_MGT_EXTENSIONS) {
		iser_info("FastReg supported, using FastReg for registration\n");
		device->iser_alloc_rdma_reg_res = iser_create_fastreg_pool;
		device->iser_free_rdma_reg_res = iser_free_fastreg_pool;
		device->iser_reg_rdma_mem = iser_reg_rdma_mem_fastreg;
		device->iser_unreg_rdma_mem = iser_unreg_mem_fastreg;
	} else {
		iser_err("IB device does not support FMRs nor FastRegs, can't register memory\n");
		return -1;
	}

	device->cqs_used = min(ISER_MAX_CQ, device->ib_device->num_comp_vectors);
	iser_info("using %d CQs, device %s supports %d vectors\n",
		  device->cqs_used, device->ib_device->name,
		  device->ib_device->num_comp_vectors);

	device->cq_desc = kmalloc(sizeof(struct iser_cq_desc) * device->cqs_used,
				  GFP_KERNEL);
	if (device->cq_desc == NULL)
		goto cq_desc_err;
	cq_desc = device->cq_desc;

	device->pd = ib_alloc_pd(device->ib_device);
	if (IS_ERR(device->pd))
		goto pd_err;

	for (i = 0; i < device->cqs_used; i++) {
		cq_desc[i].device   = device;
		cq_desc[i].cq_index = i;

		device->rx_cq[i] = ib_create_cq(device->ib_device,
					  iser_cq_callback,
					  iser_cq_event_callback,
					  (void *)&cq_desc[i],
					  ISER_MAX_RX_CQ_LEN, i);
		if (IS_ERR(device->rx_cq[i]))
			goto cq_err;

		device->tx_cq[i] = ib_create_cq(device->ib_device,
					  NULL, iser_cq_event_callback,
					  (void *)&cq_desc[i],
					  ISER_MAX_TX_CQ_LEN, i);

		if (IS_ERR(device->tx_cq[i]))
			goto cq_err;

		if (ib_req_notify_cq(device->rx_cq[i], IB_CQ_NEXT_COMP))
			goto cq_err;

		tasklet_init(&device->cq_tasklet[i],
			     iser_cq_tasklet_fn,
			(unsigned long)&cq_desc[i]);
	}

	device->mr = ib_get_dma_mr(device->pd, IB_ACCESS_LOCAL_WRITE |
				   IB_ACCESS_REMOTE_WRITE |
				   IB_ACCESS_REMOTE_READ);
	if (IS_ERR(device->mr))
		goto dma_mr_err;

	INIT_IB_EVENT_HANDLER(&device->event_handler, device->ib_device,
				iser_event_handler);
	if (ib_register_event_handler(&device->event_handler))
		goto handler_err;

	return 0;

handler_err:
	ib_dereg_mr(device->mr);
dma_mr_err:
	for (j = 0; j < device->cqs_used; j++)
		tasklet_kill(&device->cq_tasklet[j]);
cq_err:
	for (j = 0; j < i; j++) {
		if (device->tx_cq[j])
			ib_destroy_cq(device->tx_cq[j]);
		if (device->rx_cq[j])
			ib_destroy_cq(device->rx_cq[j]);
	}
	ib_dealloc_pd(device->pd);
pd_err:
	kfree(device->cq_desc);
cq_desc_err:
	iser_err("failed to allocate an IB resource\n");
	return -1;
}

/**
 * iser_free_device_ib_res - destroy/dealloc/dereg the DMA MR,
 * CQ and PD created with the device associated with the adapator.
 */
static void iser_free_device_ib_res(struct iser_device *device)
{
	int i;
	BUG_ON(device->mr == NULL);

	for (i = 0; i < device->cqs_used; i++) {
		tasklet_kill(&device->cq_tasklet[i]);
		(void)ib_destroy_cq(device->tx_cq[i]);
		(void)ib_destroy_cq(device->rx_cq[i]);
		device->tx_cq[i] = NULL;
		device->rx_cq[i] = NULL;
	}

	(void)ib_unregister_event_handler(&device->event_handler);
	(void)ib_dereg_mr(device->mr);
	(void)ib_dealloc_pd(device->pd);

	kfree(device->cq_desc);

	device->mr = NULL;
	device->pd = NULL;
}

/**
 * iser_create_fmr_pool - Creates FMR pool and page_vector
 *
 * returns 0 on success, or errno code on failure
 */
int iser_create_fmr_pool(struct iser_conn *ib_conn, unsigned cmds_max)
{
	struct iser_device *device = ib_conn->device;
	struct ib_fmr_pool_param params;
	int ret = -ENOMEM;

	ib_conn->fmr.page_vec = kmalloc(sizeof(*ib_conn->fmr.page_vec) +
					(sizeof(u64)*(ISCSI_ISER_SG_TABLESIZE + 1)),
					GFP_KERNEL);
	if (!ib_conn->fmr.page_vec)
		return ret;

	ib_conn->fmr.page_vec->pages = (u64 *)(ib_conn->fmr.page_vec + 1);

	params.page_shift        = SHIFT_4K;
	/* when the first/last SG element are not start/end *
	 * page aligned, the map whould be of N+1 pages     */
	params.max_pages_per_fmr = ISCSI_ISER_SG_TABLESIZE + 1;
	/* make the pool size twice the max number of SCSI commands *
	 * the ML is expected to queue, watermark for unmap at 50%  */
	params.pool_size	 = cmds_max * 2;
	params.dirty_watermark	 = cmds_max;
	params.cache		 = 0;
	params.flush_function	 = NULL;
	params.access		 = (IB_ACCESS_LOCAL_WRITE  |
				    IB_ACCESS_REMOTE_WRITE |
				    IB_ACCESS_REMOTE_READ);

	ib_conn->fmr.pool = ib_create_fmr_pool(device->pd, &params);
	if (!IS_ERR(ib_conn->fmr.pool))
		return 0;

	/* no FMR => no need for page_vec */
	kfree(ib_conn->fmr.page_vec);
	ib_conn->fmr.page_vec = NULL;

	ret = PTR_ERR(ib_conn->fmr.pool);
	ib_conn->fmr.pool = NULL;
	if (ret != -ENOSYS) {
		iser_err("FMR allocation failed, err %d\n", ret);
		return ret;
	} else {
		iser_warn("FMRs are not supported, using unaligned mode\n");
		return 0;
	}
}

/**
 * iser_free_fmr_pool - releases the FMR pool and page vec
 */
void iser_free_fmr_pool(struct iser_conn *ib_conn)
{
	iser_info("freeing conn %p fmr pool %p\n",
		  ib_conn, ib_conn->fmr.pool);

	if (ib_conn->fmr.pool != NULL)
		ib_destroy_fmr_pool(ib_conn->fmr.pool);

	ib_conn->fmr.pool = NULL;

	kfree(ib_conn->fmr.page_vec);
	ib_conn->fmr.page_vec = NULL;
}

static int
iser_create_fastreg_desc(struct ib_device *ib_device, struct ib_pd *pd,
			 bool pi_enable, struct fast_reg_descriptor *desc)
{
	int ret;

	desc->data_frpl = ib_alloc_fast_reg_page_list(ib_device,
						      ISCSI_ISER_SG_TABLESIZE + 1);
	if (IS_ERR(desc->data_frpl)) {
		ret = PTR_ERR(desc->data_frpl);
		iser_err("Failed to allocate ib_fast_reg_page_list err=%d\n",
			 ret);
		return PTR_ERR(desc->data_frpl);
	}

	desc->data_mr = ib_alloc_fast_reg_mr(pd, ISCSI_ISER_SG_TABLESIZE + 1);
	if (IS_ERR(desc->data_mr)) {
		ret = PTR_ERR(desc->data_mr);
		iser_err("Failed to allocate ib_fast_reg_mr err=%d\n", ret);
		goto fast_reg_mr_failure;
	}
	desc->reg_indicators |= ISER_DATA_KEY_VALID;

	if (pi_enable) {
		struct ib_mr_init_attr mr_init_attr = {0};
		struct iser_pi_context *pi_ctx = NULL;

		desc->pi_ctx = kzalloc(sizeof(*desc->pi_ctx), GFP_KERNEL);
		if (!desc->pi_ctx) {
			iser_err("Failed to allocate pi context\n");
			ret = -ENOMEM;
			goto pi_ctx_alloc_failure;
		}
		pi_ctx = desc->pi_ctx;

		pi_ctx->prot_frpl = ib_alloc_fast_reg_page_list(ib_device,
						    ISCSI_ISER_SG_TABLESIZE);
		if (IS_ERR(pi_ctx->prot_frpl)) {
			ret = PTR_ERR(pi_ctx->prot_frpl);
			iser_err("Failed to allocate prot frpl ret=%d\n",
				 ret);
			goto prot_frpl_failure;
		}

		pi_ctx->prot_mr = ib_alloc_fast_reg_mr(pd,
						ISCSI_ISER_SG_TABLESIZE + 1);
		if (IS_ERR(pi_ctx->prot_mr)) {
			ret = PTR_ERR(pi_ctx->prot_mr);
			iser_err("Failed to allocate prot frmr ret=%d\n",
				 ret);
			goto prot_mr_failure;
		}
		desc->reg_indicators |= ISER_PROT_KEY_VALID;

		mr_init_attr.max_reg_descriptors = 2;
		mr_init_attr.flags |= IB_MR_SIGNATURE_EN;
		pi_ctx->sig_mr = ib_create_mr(pd, &mr_init_attr);
		if (IS_ERR(pi_ctx->sig_mr)) {
			ret = PTR_ERR(pi_ctx->sig_mr);
			iser_err("Failed to allocate signature enabled mr err=%d\n",
				 ret);
			goto sig_mr_failure;
		}
		desc->reg_indicators |= ISER_SIG_KEY_VALID;
	}
	desc->reg_indicators &= ~ISER_FASTREG_PROTECTED;

	iser_dbg("Create fr_desc %p page_list %p\n",
		 desc, desc->data_frpl->page_list);

	return 0;
sig_mr_failure:
	ib_dereg_mr(desc->pi_ctx->prot_mr);
prot_mr_failure:
	ib_free_fast_reg_page_list(desc->pi_ctx->prot_frpl);
prot_frpl_failure:
	kfree(desc->pi_ctx);
pi_ctx_alloc_failure:
	ib_dereg_mr(desc->data_mr);
fast_reg_mr_failure:
	ib_free_fast_reg_page_list(desc->data_frpl);

	return ret;
}

/**
 * iser_create_fastreg_pool - Creates pool of fast_reg descriptors
 * for fast registration work requests.
 * returns 0 on success, or errno code on failure
 */
int iser_create_fastreg_pool(struct iser_conn *ib_conn, unsigned cmds_max)
{
	struct iser_device	*device = ib_conn->device;
	struct fast_reg_descriptor	*desc;
	int i, ret;

	INIT_LIST_HEAD(&ib_conn->fastreg.pool);
	ib_conn->fastreg.pool_size = 0;
	for (i = 0; i < cmds_max; i++) {
		desc = kzalloc(sizeof(*desc), GFP_KERNEL);
		if (!desc) {
			iser_err("Failed to allocate a new fast_reg descriptor\n");
			ret = -ENOMEM;
			goto err;
		}

		ret = iser_create_fastreg_desc(device->ib_device, device->pd,
					       ib_conn->pi_support, desc);
		if (ret) {
			iser_err("Failed to create fastreg descriptor err=%d\n",
				 ret);
			kfree(desc);
			goto err;
		}

		list_add_tail(&desc->list, &ib_conn->fastreg.pool);
		ib_conn->fastreg.pool_size++;
	}

	return 0;

err:
	iser_free_fastreg_pool(ib_conn);
	return ret;
}

/**
 * iser_free_fastreg_pool - releases the pool of fast_reg descriptors
 */
void iser_free_fastreg_pool(struct iser_conn *ib_conn)
{
	struct fast_reg_descriptor *desc, *tmp;
	int i = 0;

	if (list_empty(&ib_conn->fastreg.pool))
		return;

	iser_info("freeing conn %p fr pool\n", ib_conn);

	list_for_each_entry_safe(desc, tmp, &ib_conn->fastreg.pool, list) {
		list_del(&desc->list);
		ib_free_fast_reg_page_list(desc->data_frpl);
		ib_dereg_mr(desc->data_mr);
		if (desc->pi_ctx) {
			ib_free_fast_reg_page_list(desc->pi_ctx->prot_frpl);
			ib_dereg_mr(desc->pi_ctx->prot_mr);
			ib_destroy_mr(desc->pi_ctx->sig_mr);
			kfree(desc->pi_ctx);
		}
		kfree(desc);
		++i;
	}

	if (i < ib_conn->fastreg.pool_size)
		iser_warn("pool still has %d regions registered\n",
			  ib_conn->fastreg.pool_size - i);
}

/**
 * iser_create_ib_conn_res - Queue-Pair (QP)
 *
 * returns 0 on success, -1 on failure
 */
static int iser_create_ib_conn_res(struct iser_conn *ib_conn)
{
	struct iser_device	*device;
	struct ib_qp_init_attr	init_attr;
	int			ret = -ENOMEM;
	int index, min_index = 0;

	BUG_ON(ib_conn->device == NULL);

	device = ib_conn->device;

	memset(&init_attr, 0, sizeof init_attr);

	mutex_lock(&ig.connlist_mutex);
	/* select the CQ with the minimal number of usages */
	for (index = 0; index < device->cqs_used; index++)
		if (device->cq_active_qps[index] <
		    device->cq_active_qps[min_index])
			min_index = index;
	device->cq_active_qps[min_index]++;
	mutex_unlock(&ig.connlist_mutex);
	iser_info("cq index %d used for ib_conn %p\n", min_index, ib_conn);

	init_attr.event_handler = iser_qp_event_callback;
	init_attr.qp_context	= (void *)ib_conn;
	init_attr.send_cq	= device->tx_cq[min_index];
	init_attr.recv_cq	= device->rx_cq[min_index];
	init_attr.cap.max_recv_wr  = ISER_QP_MAX_RECV_DTOS;
	init_attr.cap.max_send_sge = 2;
	init_attr.cap.max_recv_sge = 1;
	init_attr.sq_sig_type	= IB_SIGNAL_REQ_WR;
	init_attr.qp_type	= IB_QPT_RC;
	if (ib_conn->pi_support) {
		init_attr.cap.max_send_wr = ISER_QP_SIG_MAX_REQ_DTOS;
		init_attr.create_flags |= IB_QP_CREATE_SIGNATURE_EN;
	} else {
		init_attr.cap.max_send_wr  = ISER_QP_MAX_REQ_DTOS;
	}

	ret = rdma_create_qp(ib_conn->cma_id, device->pd, &init_attr);
	if (ret)
		goto out_err;

	ib_conn->qp = ib_conn->cma_id->qp;
	iser_info("setting conn %p cma_id %p qp %p\n",
		  ib_conn, ib_conn->cma_id,
		  ib_conn->cma_id->qp);
	return ret;

out_err:
	iser_err("unable to alloc mem or create resource, err %d\n", ret);
	return ret;
}

/**
 * releases the QP object
 */
static void iser_free_ib_conn_res(struct iser_conn *ib_conn)
{
	int cq_index;
	BUG_ON(ib_conn == NULL);

	iser_info("freeing conn %p cma_id %p qp %p\n",
		  ib_conn, ib_conn->cma_id,
		  ib_conn->qp);

	/* qp is created only once both addr & route are resolved */

	if (ib_conn->qp != NULL) {
		cq_index = ((struct iser_cq_desc *)ib_conn->qp->recv_cq->cq_context)->cq_index;
		ib_conn->device->cq_active_qps[cq_index]--;

		rdma_destroy_qp(ib_conn->cma_id);
	}

	ib_conn->qp	  = NULL;
}

/**
 * based on the resolved device node GUID see if there already allocated
 * device for this device. If there's no such, create one.
 */
static
struct iser_device *iser_device_find_by_ib_device(struct rdma_cm_id *cma_id)
{
	struct iser_device *device;

	mutex_lock(&ig.device_list_mutex);

	list_for_each_entry(device, &ig.device_list, ig_list)
		/* find if there's a match using the node GUID */
		if (device->ib_device->node_guid == cma_id->device->node_guid)
			goto inc_refcnt;

	device = kzalloc(sizeof *device, GFP_KERNEL);
	if (device == NULL)
		goto out;

	/* assign this device to the device */
	device->ib_device = cma_id->device;
	/* init the device and link it into ig device list */
	if (iser_create_device_ib_res(device)) {
		kfree(device);
		device = NULL;
		goto out;
	}
	list_add(&device->ig_list, &ig.device_list);

inc_refcnt:
	device->refcount++;
out:
	mutex_unlock(&ig.device_list_mutex);
	return device;
}

/* if there's no demand for this device, release it */
static void iser_device_try_release(struct iser_device *device)
{
	mutex_lock(&ig.device_list_mutex);
	device->refcount--;
	iser_info("device %p refcount %d\n", device, device->refcount);
	if (!device->refcount) {
		iser_free_device_ib_res(device);
		list_del(&device->ig_list);
		kfree(device);
	}
	mutex_unlock(&ig.device_list_mutex);
}

/**
 * Called with state mutex held
 **/
static int iser_conn_state_comp_exch(struct iser_conn *ib_conn,
				     enum iser_ib_conn_state comp,
				     enum iser_ib_conn_state exch)
{
	int ret;

	if ((ret = (ib_conn->state == comp)))
		ib_conn->state = exch;
	return ret;
}

void iser_release_work(struct work_struct *work)
{
	struct iser_conn *ib_conn;
	int rc;

	ib_conn = container_of(work, struct iser_conn, release_work);

	/* wait for .conn_stop callback */
	rc = wait_for_completion_timeout(&ib_conn->stop_completion, 30 * HZ);
	WARN_ON(rc == 0);

	/* wait for the qp`s post send and post receive buffers to empty */
	rc = wait_for_completion_timeout(&ib_conn->flush_completion, 30 * HZ);
	WARN_ON(rc == 0);

	ib_conn->state = ISER_CONN_DOWN;

	mutex_lock(&ib_conn->state_mutex);
	ib_conn->state = ISER_CONN_DOWN;
	mutex_unlock(&ib_conn->state_mutex);

	iser_conn_release(ib_conn);
}

/**
 * Frees all conn objects and deallocs conn descriptor
 */
void iser_conn_release(struct iser_conn *ib_conn)
{
	struct iser_device  *device = ib_conn->device;

	mutex_lock(&ig.connlist_mutex);
	list_del(&ib_conn->conn_list);
	mutex_unlock(&ig.connlist_mutex);

	mutex_lock(&ib_conn->state_mutex);
	BUG_ON(ib_conn->state != ISER_CONN_DOWN);

	iser_free_rx_descriptors(ib_conn);
	iser_free_ib_conn_res(ib_conn);
	ib_conn->device = NULL;
	/* on EVENT_ADDR_ERROR there's no device yet for this conn */
	if (device != NULL)
		iser_device_try_release(device);
	mutex_unlock(&ib_conn->state_mutex);

	/* if cma handler context, the caller actually destroy the id */
	if (ib_conn->cma_id != NULL) {
		rdma_destroy_id(ib_conn->cma_id);
		ib_conn->cma_id = NULL;
	}
	kfree(ib_conn);
}

/**
 * triggers start of the disconnect procedures and wait for them to be done
 */
void iser_conn_terminate(struct iser_conn *ib_conn)
{
	int err = 0;

	/* change the ib conn state only if the conn is UP, however always call
	 * rdma_disconnect since this is the only way to cause the CMA to change
	 * the QP state to ERROR
	 */

	iser_conn_state_comp_exch(ib_conn, ISER_CONN_UP, ISER_CONN_TERMINATING);
	err = rdma_disconnect(ib_conn->cma_id);
	if (err)
		iser_err("Failed to disconnect, conn: 0x%p err %d\n",
			 ib_conn,err);
}

/**
 * Called with state mutex held
 **/
static void iser_connect_error(struct rdma_cm_id *cma_id)
{
	struct iser_conn *ib_conn;

	ib_conn = (struct iser_conn *)cma_id->context;
	ib_conn->state = ISER_CONN_DOWN;
}

/**
 * Called with state mutex held
 **/
static void iser_addr_handler(struct rdma_cm_id *cma_id)
{
	struct iser_device *device;
	struct iser_conn   *ib_conn;
	int    ret;

	ib_conn = (struct iser_conn *)cma_id->context;
	if (ib_conn->state != ISER_CONN_PENDING)
		/* bailout */
		return;

	device = iser_device_find_by_ib_device(cma_id);
	if (!device) {
		iser_err("device lookup/creation failed\n");
		iser_connect_error(cma_id);
		return;
	}

	ib_conn->device = device;

	/* connection T10-PI support */
	if (iser_pi_enable) {
		if (!(device->dev_attr.device_cap_flags &
		      IB_DEVICE_SIGNATURE_HANDOVER)) {
			iser_warn("T10-PI requested but not supported on %s, "
				  "continue without T10-PI\n",
				  ib_conn->device->ib_device->name);
			ib_conn->pi_support = false;
		} else {
			ib_conn->pi_support = true;
		}
	}

	ret = rdma_resolve_route(cma_id, 1000);
	if (ret) {
		iser_err("resolve route failed: %d\n", ret);
		iser_connect_error(cma_id);
		return;
	}
}

/**
 * Called with state mutex held
 **/
static void iser_route_handler(struct rdma_cm_id *cma_id)
{
	struct rdma_conn_param conn_param;
	int    ret;
	struct iser_cm_hdr req_hdr;
	struct iser_conn *ib_conn = (struct iser_conn *)cma_id->context;
	struct iser_device *device = ib_conn->device;

	if (ib_conn->state != ISER_CONN_PENDING)
		/* bailout */
		return;

	ret = iser_create_ib_conn_res((struct iser_conn *)cma_id->context);
	if (ret)
		goto failure;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = device->dev_attr.max_qp_rd_atom;
	conn_param.initiator_depth     = 1;
	conn_param.retry_count	       = 7;
	conn_param.rnr_retry_count     = 6;

	memset(&req_hdr, 0, sizeof(req_hdr));
	req_hdr.flags = (ISER_ZBVA_NOT_SUPPORTED |
			ISER_SEND_W_INV_NOT_SUPPORTED);
	conn_param.private_data		= (void *)&req_hdr;
	conn_param.private_data_len	= sizeof(struct iser_cm_hdr);

	ret = rdma_connect(cma_id, &conn_param);
	if (ret) {
		iser_err("failure connecting: %d\n", ret);
		goto failure;
	}

	return;
failure:
	iser_connect_error(cma_id);
}

static void iser_connected_handler(struct rdma_cm_id *cma_id)
{
	struct iser_conn *ib_conn;
	struct ib_qp_attr attr;
	struct ib_qp_init_attr init_attr;

	ib_conn = (struct iser_conn *)cma_id->context;
	if (ib_conn->state != ISER_CONN_PENDING)
		/* bailout */
		return;

	(void)ib_query_qp(cma_id->qp, &attr, ~0, &init_attr);
	iser_info("remote qpn:%x my qpn:%x\n", attr.dest_qp_num, cma_id->qp->qp_num);

	ib_conn->state = ISER_CONN_UP;
	complete(&ib_conn->up_completion);
}

static void iser_disconnected_handler(struct rdma_cm_id *cma_id)
{
	struct iser_conn *ib_conn;

	ib_conn = (struct iser_conn *)cma_id->context;

	/* getting here when the state is UP means that the conn is being *
	 * terminated asynchronously from the iSCSI layer's perspective.  */
	if (iser_conn_state_comp_exch(ib_conn, ISER_CONN_UP,
					ISER_CONN_TERMINATING)){
		if (ib_conn->iscsi_conn)
			iscsi_conn_failure(ib_conn->iscsi_conn, ISCSI_ERR_CONN_FAILED);
		else
			iser_err("iscsi_iser connection isn't bound\n");
	}

	/* Complete the termination process if no posts are pending */
	if (ib_conn->post_recv_buf_count == 0 &&
	    (atomic_read(&ib_conn->post_send_buf_count) == 0)) {
		complete(&ib_conn->flush_completion);
	}
}

static int iser_cma_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	struct iser_conn *ib_conn;

	ib_conn = (struct iser_conn *)cma_id->context;
	iser_info("event %d status %d conn %p id %p\n",
		  event->event, event->status, cma_id->context, cma_id);

	mutex_lock(&ib_conn->state_mutex);
	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		iser_addr_handler(cma_id);
		break;
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
		iser_route_handler(cma_id);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		iser_connected_handler(cma_id);
		break;
	case RDMA_CM_EVENT_ADDR_ERROR:
	case RDMA_CM_EVENT_ROUTE_ERROR:
	case RDMA_CM_EVENT_CONNECT_ERROR:
	case RDMA_CM_EVENT_UNREACHABLE:
	case RDMA_CM_EVENT_REJECTED:
		iser_connect_error(cma_id);
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
	case RDMA_CM_EVENT_ADDR_CHANGE:
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		iser_disconnected_handler(cma_id);
		break;
	default:
		iser_err("Unexpected RDMA CM event (%d)\n", event->event);
		break;
	}
	mutex_unlock(&ib_conn->state_mutex);
	return 0;
}

void iser_conn_init(struct iser_conn *ib_conn)
{
	ib_conn->state = ISER_CONN_INIT;
	ib_conn->post_recv_buf_count = 0;
	atomic_set(&ib_conn->post_send_buf_count, 0);
	init_completion(&ib_conn->stop_completion);
	init_completion(&ib_conn->flush_completion);
	init_completion(&ib_conn->up_completion);
	INIT_LIST_HEAD(&ib_conn->conn_list);
	spin_lock_init(&ib_conn->lock);
	mutex_init(&ib_conn->state_mutex);
}

 /**
 * starts the process of connecting to the target
 * sleeps until the connection is established or rejected
 */
int iser_connect(struct iser_conn   *ib_conn,
		 struct sockaddr    *src_addr,
		 struct sockaddr    *dst_addr,
		 int                 non_blocking)
{
	int err = 0;

	mutex_lock(&ib_conn->state_mutex);

	sprintf(ib_conn->name, "%pISp", dst_addr);

	iser_info("connecting to: %s\n", ib_conn->name);

	/* the device is known only --after-- address resolution */
	ib_conn->device = NULL;

	ib_conn->state = ISER_CONN_PENDING;

	ib_conn->cma_id = rdma_create_id(iser_cma_handler,
					     (void *)ib_conn,
					     RDMA_PS_TCP, IB_QPT_RC);
	if (IS_ERR(ib_conn->cma_id)) {
		err = PTR_ERR(ib_conn->cma_id);
		iser_err("rdma_create_id failed: %d\n", err);
		goto id_failure;
	}

	err = rdma_resolve_addr(ib_conn->cma_id, src_addr, dst_addr, 1000);
	if (err) {
		iser_err("rdma_resolve_addr failed: %d\n", err);
		goto addr_failure;
	}

	if (!non_blocking) {
		wait_for_completion_interruptible(&ib_conn->up_completion);

		if (ib_conn->state != ISER_CONN_UP) {
			err =  -EIO;
			goto connect_failure;
		}
	}
	mutex_unlock(&ib_conn->state_mutex);

	mutex_lock(&ig.connlist_mutex);
	list_add(&ib_conn->conn_list, &ig.connlist);
	mutex_unlock(&ig.connlist_mutex);
	return 0;

id_failure:
	ib_conn->cma_id = NULL;
addr_failure:
	ib_conn->state = ISER_CONN_DOWN;
connect_failure:
	mutex_unlock(&ib_conn->state_mutex);
	iser_conn_release(ib_conn);
	return err;
}

/**
 * iser_reg_page_vec - Register physical memory
 *
 * returns: 0 on success, errno code on failure
 */
int iser_reg_page_vec(struct iser_conn     *ib_conn,
		      struct iser_page_vec *page_vec,
		      struct iser_mem_reg  *mem_reg)
{
	struct ib_pool_fmr *mem;
	u64		   io_addr;
	u64		   *page_list;
	int		   status;

	page_list = page_vec->pages;
	io_addr	  = page_list[0];

	mem  = ib_fmr_pool_map_phys(ib_conn->fmr.pool,
				    page_list,
				    page_vec->length,
				    io_addr);

	if (IS_ERR(mem)) {
		status = (int)PTR_ERR(mem);
		iser_err("ib_fmr_pool_map_phys failed: %d\n", status);
		return status;
	}

	mem_reg->lkey  = mem->fmr->lkey;
	mem_reg->rkey  = mem->fmr->rkey;
	mem_reg->len   = page_vec->length * SIZE_4K;
	mem_reg->va    = io_addr;
	mem_reg->is_mr = 1;
	mem_reg->mem_h = (void *)mem;

	mem_reg->va   += page_vec->offset;
	mem_reg->len   = page_vec->data_size;

	iser_dbg("PHYSICAL Mem.register, [PHYS p_array: 0x%p, sz: %d, "
		 "entry[0]: (0x%08lx,%ld)] -> "
		 "[lkey: 0x%08X mem_h: 0x%p va: 0x%08lX sz: %ld]\n",
		 page_vec, page_vec->length,
		 (unsigned long)page_vec->pages[0],
		 (unsigned long)page_vec->data_size,
		 (unsigned int)mem_reg->lkey, mem_reg->mem_h,
		 (unsigned long)mem_reg->va, (unsigned long)mem_reg->len);
	return 0;
}

/**
 * Unregister (previosuly registered using FMR) memory.
 * If memory is non-FMR does nothing.
 */
void iser_unreg_mem_fmr(struct iscsi_iser_task *iser_task,
			enum iser_data_dir cmd_dir)
{
	struct iser_mem_reg *reg = &iser_task->rdma_regd[cmd_dir].reg;
	int ret;

	if (!reg->is_mr)
		return;

	iser_dbg("PHYSICAL Mem.Unregister mem_h %p\n",reg->mem_h);

	ret = ib_fmr_pool_unmap((struct ib_pool_fmr *)reg->mem_h);
	if (ret)
		iser_err("ib_fmr_pool_unmap failed %d\n", ret);

	reg->mem_h = NULL;
}

void iser_unreg_mem_fastreg(struct iscsi_iser_task *iser_task,
			    enum iser_data_dir cmd_dir)
{
	struct iser_mem_reg *reg = &iser_task->rdma_regd[cmd_dir].reg;
	struct iser_conn *ib_conn = iser_task->ib_conn;
	struct fast_reg_descriptor *desc = reg->mem_h;

	if (!reg->is_mr)
		return;

	reg->mem_h = NULL;
	reg->is_mr = 0;
	spin_lock_bh(&ib_conn->lock);
	list_add_tail(&desc->list, &ib_conn->fastreg.pool);
	spin_unlock_bh(&ib_conn->lock);
}

int iser_post_recvl(struct iser_conn *ib_conn)
{
	struct ib_recv_wr rx_wr, *rx_wr_failed;
	struct ib_sge	  sge;
	int ib_ret;

	sge.addr   = ib_conn->login_resp_dma;
	sge.length = ISER_RX_LOGIN_SIZE;
	sge.lkey   = ib_conn->device->mr->lkey;

	rx_wr.wr_id   = (unsigned long)ib_conn->login_resp_buf;
	rx_wr.sg_list = &sge;
	rx_wr.num_sge = 1;
	rx_wr.next    = NULL;

	ib_conn->post_recv_buf_count++;
	ib_ret	= ib_post_recv(ib_conn->qp, &rx_wr, &rx_wr_failed);
	if (ib_ret) {
		iser_err("ib_post_recv failed ret=%d\n", ib_ret);
		ib_conn->post_recv_buf_count--;
	}
	return ib_ret;
}

int iser_post_recvm(struct iser_conn *ib_conn, int count)
{
	struct ib_recv_wr *rx_wr, *rx_wr_failed;
	int i, ib_ret;
	unsigned int my_rx_head = ib_conn->rx_desc_head;
	struct iser_rx_desc *rx_desc;

	for (rx_wr = ib_conn->rx_wr, i = 0; i < count; i++, rx_wr++) {
		rx_desc		= &ib_conn->rx_descs[my_rx_head];
		rx_wr->wr_id	= (unsigned long)rx_desc;
		rx_wr->sg_list	= &rx_desc->rx_sg;
		rx_wr->num_sge	= 1;
		rx_wr->next	= rx_wr + 1;
		my_rx_head = (my_rx_head + 1) & ib_conn->qp_max_recv_dtos_mask;
	}

	rx_wr--;
	rx_wr->next = NULL; /* mark end of work requests list */

	ib_conn->post_recv_buf_count += count;
	ib_ret	= ib_post_recv(ib_conn->qp, ib_conn->rx_wr, &rx_wr_failed);
	if (ib_ret) {
		iser_err("ib_post_recv failed ret=%d\n", ib_ret);
		ib_conn->post_recv_buf_count -= count;
	} else
		ib_conn->rx_desc_head = my_rx_head;
	return ib_ret;
}


/**
 * iser_start_send - Initiate a Send DTO operation
 *
 * returns 0 on success, -1 on failure
 */
int iser_post_send(struct iser_conn *ib_conn, struct iser_tx_desc *tx_desc)
{
	int		  ib_ret;
	struct ib_send_wr send_wr, *send_wr_failed;

	ib_dma_sync_single_for_device(ib_conn->device->ib_device,
		tx_desc->dma_addr, ISER_HEADERS_LEN, DMA_TO_DEVICE);

	send_wr.next	   = NULL;
	send_wr.wr_id	   = (unsigned long)tx_desc;
	send_wr.sg_list	   = tx_desc->tx_sg;
	send_wr.num_sge	   = tx_desc->num_sge;
	send_wr.opcode	   = IB_WR_SEND;
	send_wr.send_flags = IB_SEND_SIGNALED;

	atomic_inc(&ib_conn->post_send_buf_count);

	ib_ret = ib_post_send(ib_conn->qp, &send_wr, &send_wr_failed);
	if (ib_ret) {
		iser_err("ib_post_send failed, ret:%d\n", ib_ret);
		atomic_dec(&ib_conn->post_send_buf_count);
	}
	return ib_ret;
}

static void iser_handle_comp_error(struct iser_tx_desc *desc,
				struct iser_conn *ib_conn)
{
	if (desc && desc->type == ISCSI_TX_DATAOUT)
		kmem_cache_free(ig.desc_cache, desc);

	if (ib_conn->post_recv_buf_count == 0 &&
	    atomic_read(&ib_conn->post_send_buf_count) == 0) {
		/**
		 * getting here when the state is UP means that the conn is
		 * being terminated asynchronously from the iSCSI layer's
		 * perspective. It is safe to peek at the connection state
		 * since iscsi_conn_failure is allowed to be called twice.
		 **/
		if (ib_conn->state == ISER_CONN_UP)
			iscsi_conn_failure(ib_conn->iscsi_conn,
					   ISCSI_ERR_CONN_FAILED);

		/* no more non completed posts to the QP, complete the
		 * termination process w.o worrying on disconnect event */
		complete(&ib_conn->flush_completion);
	}
}

static int iser_drain_tx_cq(struct iser_device  *device, int cq_index)
{
	struct ib_cq  *cq = device->tx_cq[cq_index];
	struct ib_wc  wc;
	struct iser_tx_desc *tx_desc;
	struct iser_conn *ib_conn;
	int completed_tx = 0;

	while (ib_poll_cq(cq, 1, &wc) == 1) {
		tx_desc	= (struct iser_tx_desc *) (unsigned long) wc.wr_id;
		ib_conn = wc.qp->qp_context;
		if (wc.status == IB_WC_SUCCESS) {
			if (wc.opcode == IB_WC_SEND)
				iser_snd_completion(tx_desc, ib_conn);
			else
				iser_err("expected opcode %d got %d\n",
					IB_WC_SEND, wc.opcode);
		} else {
			iser_err("tx id %llx status %d vend_err %x\n",
				 wc.wr_id, wc.status, wc.vendor_err);
			if (wc.wr_id != ISER_FASTREG_LI_WRID) {
				atomic_dec(&ib_conn->post_send_buf_count);
				iser_handle_comp_error(tx_desc, ib_conn);
			}
		}
		completed_tx++;
	}
	return completed_tx;
}


static void iser_cq_tasklet_fn(unsigned long data)
{
	struct iser_cq_desc *cq_desc = (struct iser_cq_desc *)data;
	struct iser_device  *device = cq_desc->device;
	int cq_index = cq_desc->cq_index;
	struct ib_cq	     *cq = device->rx_cq[cq_index];
	 struct ib_wc	     wc;
	 struct iser_rx_desc *desc;
	 unsigned long	     xfer_len;
	struct iser_conn *ib_conn;
	int completed_tx, completed_rx = 0;

	/* First do tx drain, so in a case where we have rx flushes and a successful
	 * tx completion we will still go through completion error handling.
	 */
	completed_tx = iser_drain_tx_cq(device, cq_index);

	while (ib_poll_cq(cq, 1, &wc) == 1) {
		desc	 = (struct iser_rx_desc *) (unsigned long) wc.wr_id;
		BUG_ON(desc == NULL);
		ib_conn = wc.qp->qp_context;
		if (wc.status == IB_WC_SUCCESS) {
			if (wc.opcode == IB_WC_RECV) {
				xfer_len = (unsigned long)wc.byte_len;
				iser_rcv_completion(desc, xfer_len, ib_conn);
			} else
				iser_err("expected opcode %d got %d\n",
					IB_WC_RECV, wc.opcode);
		} else {
			if (wc.status != IB_WC_WR_FLUSH_ERR)
				iser_err("rx id %llx status %d vend_err %x\n",
					wc.wr_id, wc.status, wc.vendor_err);
			ib_conn->post_recv_buf_count--;
			iser_handle_comp_error(NULL, ib_conn);
		}
		completed_rx++;
		if (!(completed_rx & 63))
			completed_tx += iser_drain_tx_cq(device, cq_index);
	}
	/* #warning "it is assumed here that arming CQ only once its empty" *
	 * " would not cause interrupts to be missed"                       */
	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);

	iser_dbg("got %d rx %d tx completions\n", completed_rx, completed_tx);
}

static void iser_cq_callback(struct ib_cq *cq, void *cq_context)
{
	struct iser_cq_desc *cq_desc = (struct iser_cq_desc *)cq_context;
	struct iser_device  *device = cq_desc->device;
	int cq_index = cq_desc->cq_index;

	tasklet_schedule(&device->cq_tasklet[cq_index]);
}

u8 iser_check_task_pi_status(struct iscsi_iser_task *iser_task,
			     enum iser_data_dir cmd_dir, sector_t *sector)
{
	struct iser_mem_reg *reg = &iser_task->rdma_regd[cmd_dir].reg;
	struct fast_reg_descriptor *desc = reg->mem_h;
	unsigned long sector_size = iser_task->sc->device->sector_size;
	struct ib_mr_status mr_status;
	int ret;

	if (desc && desc->reg_indicators & ISER_FASTREG_PROTECTED) {
		desc->reg_indicators &= ~ISER_FASTREG_PROTECTED;
		ret = ib_check_mr_status(desc->pi_ctx->sig_mr,
					 IB_MR_CHECK_SIG_STATUS, &mr_status);
		if (ret) {
			pr_err("ib_check_mr_status failed, ret %d\n", ret);
			goto err;
		}

		if (mr_status.fail_status & IB_MR_CHECK_SIG_STATUS) {
			sector_t sector_off = mr_status.sig_err.sig_err_offset;

			do_div(sector_off, sector_size + 8);
			*sector = scsi_get_lba(iser_task->sc) + sector_off;

			pr_err("PI error found type %d at sector %llx "
			       "expected %x vs actual %x\n",
			       mr_status.sig_err.err_type,
			       (unsigned long long)*sector,
			       mr_status.sig_err.expected,
			       mr_status.sig_err.actual);

			switch (mr_status.sig_err.err_type) {
			case IB_SIG_BAD_GUARD:
				return 0x1;
			case IB_SIG_BAD_REFTAG:
				return 0x3;
			case IB_SIG_BAD_APPTAG:
				return 0x2;
			}
		}
	}

	return 0;
err:
	/* Not alot we can do here, return ambiguous guard error */
	return 0x1;
}
