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
#define ISER_MAX_RX_LEN		(ISER_QP_MAX_RECV_DTOS * ISCSI_ISER_MAX_CONN)
#define ISER_MAX_TX_LEN		(ISER_QP_MAX_REQ_DTOS  * ISCSI_ISER_MAX_CONN)
#define ISER_MAX_CQ_LEN		(ISER_MAX_RX_LEN + ISER_MAX_TX_LEN + \
				 ISCSI_ISER_MAX_CONN)

static int iser_cq_poll_limit = 512;

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
	struct ib_device_attr *dev_attr = &device->dev_attr;
	int ret, i, max_cqe;

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

	device->comps_used = min(ISER_MAX_CQ,
				 device->ib_device->num_comp_vectors);

	max_cqe = min(ISER_MAX_CQ_LEN, dev_attr->max_cqe);

	iser_info("using %d CQs, device %s supports %d vectors max_cqe %d\n",
		  device->comps_used, device->ib_device->name,
		  device->ib_device->num_comp_vectors, max_cqe);

	device->pd = ib_alloc_pd(device->ib_device);
	if (IS_ERR(device->pd))
		goto pd_err;

	for (i = 0; i < device->comps_used; i++) {
		struct iser_comp *comp = &device->comps[i];

		comp->device = device;
		comp->cq = ib_create_cq(device->ib_device,
					iser_cq_callback,
					iser_cq_event_callback,
					(void *)comp,
					max_cqe, i);
		if (IS_ERR(comp->cq)) {
			comp->cq = NULL;
			goto cq_err;
		}

		if (ib_req_notify_cq(comp->cq, IB_CQ_NEXT_COMP))
			goto cq_err;

		tasklet_init(&comp->tasklet, iser_cq_tasklet_fn,
			     (unsigned long)comp);
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
	for (i = 0; i < device->comps_used; i++)
		tasklet_kill(&device->comps[i].tasklet);
cq_err:
	for (i = 0; i < device->comps_used; i++) {
		struct iser_comp *comp = &device->comps[i];

		if (comp->cq)
			ib_destroy_cq(comp->cq);
	}
	ib_dealloc_pd(device->pd);
pd_err:
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

	for (i = 0; i < device->comps_used; i++) {
		struct iser_comp *comp = &device->comps[i];

		tasklet_kill(&comp->tasklet);
		ib_destroy_cq(comp->cq);
		comp->cq = NULL;
	}

	(void)ib_unregister_event_handler(&device->event_handler);
	(void)ib_dereg_mr(device->mr);
	(void)ib_dealloc_pd(device->pd);

	device->mr = NULL;
	device->pd = NULL;
}

/**
 * iser_create_fmr_pool - Creates FMR pool and page_vector
 *
 * returns 0 on success, or errno code on failure
 */
int iser_create_fmr_pool(struct ib_conn *ib_conn, unsigned cmds_max)
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
void iser_free_fmr_pool(struct ib_conn *ib_conn)
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
int iser_create_fastreg_pool(struct ib_conn *ib_conn, unsigned cmds_max)
{
	struct iser_device *device = ib_conn->device;
	struct fast_reg_descriptor *desc;
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
void iser_free_fastreg_pool(struct ib_conn *ib_conn)
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
static int iser_create_ib_conn_res(struct ib_conn *ib_conn)
{
	struct iser_conn *iser_conn = container_of(ib_conn, struct iser_conn,
						   ib_conn);
	struct iser_device	*device;
	struct ib_device_attr *dev_attr;
	struct ib_qp_init_attr	init_attr;
	int			ret = -ENOMEM;
	int index, min_index = 0;

	BUG_ON(ib_conn->device == NULL);

	device = ib_conn->device;
	dev_attr = &device->dev_attr;

	memset(&init_attr, 0, sizeof init_attr);

	mutex_lock(&ig.connlist_mutex);
	/* select the CQ with the minimal number of usages */
	for (index = 0; index < device->comps_used; index++) {
		if (device->comps[index].active_qps <
		    device->comps[min_index].active_qps)
			min_index = index;
	}
	ib_conn->comp = &device->comps[min_index];
	ib_conn->comp->active_qps++;
	mutex_unlock(&ig.connlist_mutex);
	iser_info("cq index %d used for ib_conn %p\n", min_index, ib_conn);

	init_attr.event_handler = iser_qp_event_callback;
	init_attr.qp_context	= (void *)ib_conn;
	init_attr.send_cq	= ib_conn->comp->cq;
	init_attr.recv_cq	= ib_conn->comp->cq;
	init_attr.cap.max_recv_wr  = ISER_QP_MAX_RECV_DTOS;
	init_attr.cap.max_send_sge = 2;
	init_attr.cap.max_recv_sge = 1;
	init_attr.sq_sig_type	= IB_SIGNAL_REQ_WR;
	init_attr.qp_type	= IB_QPT_RC;
	if (ib_conn->pi_support) {
		init_attr.cap.max_send_wr = ISER_QP_SIG_MAX_REQ_DTOS + 1;
		init_attr.create_flags |= IB_QP_CREATE_SIGNATURE_EN;
		iser_conn->max_cmds =
			ISER_GET_MAX_XMIT_CMDS(ISER_QP_SIG_MAX_REQ_DTOS);
	} else {
		if (dev_attr->max_qp_wr > ISER_QP_MAX_REQ_DTOS) {
			init_attr.cap.max_send_wr  = ISER_QP_MAX_REQ_DTOS + 1;
			iser_conn->max_cmds =
				ISER_GET_MAX_XMIT_CMDS(ISER_QP_MAX_REQ_DTOS);
		} else {
			init_attr.cap.max_send_wr = dev_attr->max_qp_wr;
			iser_conn->max_cmds =
				ISER_GET_MAX_XMIT_CMDS(dev_attr->max_qp_wr);
			iser_dbg("device %s supports max_send_wr %d\n",
				 device->ib_device->name, dev_attr->max_qp_wr);
		}
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
	mutex_lock(&ig.connlist_mutex);
	ib_conn->comp->active_qps--;
	mutex_unlock(&ig.connlist_mutex);
	iser_err("unable to alloc mem or create resource, err %d\n", ret);

	return ret;
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
static int iser_conn_state_comp_exch(struct iser_conn *iser_conn,
				     enum iser_conn_state comp,
				     enum iser_conn_state exch)
{
	int ret;

	ret = (iser_conn->state == comp);
	if (ret)
		iser_conn->state = exch;

	return ret;
}

void iser_release_work(struct work_struct *work)
{
	struct iser_conn *iser_conn;

	iser_conn = container_of(work, struct iser_conn, release_work);

	/* Wait for conn_stop to complete */
	wait_for_completion(&iser_conn->stop_completion);
	/* Wait for IB resouces cleanup to complete */
	wait_for_completion(&iser_conn->ib_completion);

	mutex_lock(&iser_conn->state_mutex);
	iser_conn->state = ISER_CONN_DOWN;
	mutex_unlock(&iser_conn->state_mutex);

	iser_conn_release(iser_conn);
}

/**
 * iser_free_ib_conn_res - release IB related resources
 * @iser_conn: iser connection struct
 * @destroy_device: indicator if we need to try to release
 *     the iser device (only iscsi shutdown and DEVICE_REMOVAL
 *     will use this.
 *
 * This routine is called with the iser state mutex held
 * so the cm_id removal is out of here. It is Safe to
 * be invoked multiple times.
 */
static void iser_free_ib_conn_res(struct iser_conn *iser_conn,
				  bool destroy_device)
{
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;

	iser_info("freeing conn %p cma_id %p qp %p\n",
		  iser_conn, ib_conn->cma_id, ib_conn->qp);

	iser_free_rx_descriptors(iser_conn);

	if (ib_conn->qp != NULL) {
		ib_conn->comp->active_qps--;
		rdma_destroy_qp(ib_conn->cma_id);
		ib_conn->qp = NULL;
	}

	if (destroy_device && device != NULL) {
		iser_device_try_release(device);
		ib_conn->device = NULL;
	}
}

/**
 * Frees all conn objects and deallocs conn descriptor
 */
void iser_conn_release(struct iser_conn *iser_conn)
{
	struct ib_conn *ib_conn = &iser_conn->ib_conn;

	mutex_lock(&ig.connlist_mutex);
	list_del(&iser_conn->conn_list);
	mutex_unlock(&ig.connlist_mutex);

	mutex_lock(&iser_conn->state_mutex);
	if (iser_conn->state != ISER_CONN_DOWN) {
		iser_warn("iser conn %p state %d, expected state down.\n",
			  iser_conn, iser_conn->state);
		iser_conn->state = ISER_CONN_DOWN;
	}
	/*
	 * In case we never got to bind stage, we still need to
	 * release IB resources (which is safe to call more than once).
	 */
	iser_free_ib_conn_res(iser_conn, true);
	mutex_unlock(&iser_conn->state_mutex);

	if (ib_conn->cma_id != NULL) {
		rdma_destroy_id(ib_conn->cma_id);
		ib_conn->cma_id = NULL;
	}

	kfree(iser_conn);
}

/**
 * triggers start of the disconnect procedures and wait for them to be done
 * Called with state mutex held
 */
int iser_conn_terminate(struct iser_conn *iser_conn)
{
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct ib_send_wr *bad_wr;
	int err = 0;

	/* terminate the iser conn only if the conn state is UP */
	if (!iser_conn_state_comp_exch(iser_conn, ISER_CONN_UP,
				       ISER_CONN_TERMINATING))
		return 0;

	iser_info("iser_conn %p state %d\n", iser_conn, iser_conn->state);

	/* suspend queuing of new iscsi commands */
	if (iser_conn->iscsi_conn)
		iscsi_suspend_queue(iser_conn->iscsi_conn);

	/*
	 * In case we didn't already clean up the cma_id (peer initiated
	 * a disconnection), we need to Cause the CMA to change the QP
	 * state to ERROR.
	 */
	if (ib_conn->cma_id) {
		err = rdma_disconnect(ib_conn->cma_id);
		if (err)
			iser_err("Failed to disconnect, conn: 0x%p err %d\n",
				 iser_conn, err);

		/* post an indication that all flush errors were consumed */
		err = ib_post_send(ib_conn->qp, &ib_conn->beacon, &bad_wr);
		if (err) {
			iser_err("conn %p failed to post beacon", ib_conn);
			return 1;
		}

		wait_for_completion(&ib_conn->flush_comp);
	}

	return 1;
}

/**
 * Called with state mutex held
 **/
static void iser_connect_error(struct rdma_cm_id *cma_id)
{
	struct iser_conn *iser_conn;

	iser_conn = (struct iser_conn *)cma_id->context;
	iser_conn->state = ISER_CONN_DOWN;
}

/**
 * Called with state mutex held
 **/
static void iser_addr_handler(struct rdma_cm_id *cma_id)
{
	struct iser_device *device;
	struct iser_conn   *iser_conn;
	struct ib_conn   *ib_conn;
	int    ret;

	iser_conn = (struct iser_conn *)cma_id->context;
	if (iser_conn->state != ISER_CONN_PENDING)
		/* bailout */
		return;

	ib_conn = &iser_conn->ib_conn;
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
	struct iser_conn *iser_conn = (struct iser_conn *)cma_id->context;
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;

	if (iser_conn->state != ISER_CONN_PENDING)
		/* bailout */
		return;

	ret = iser_create_ib_conn_res(ib_conn);
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
	struct iser_conn *iser_conn;
	struct ib_qp_attr attr;
	struct ib_qp_init_attr init_attr;

	iser_conn = (struct iser_conn *)cma_id->context;
	if (iser_conn->state != ISER_CONN_PENDING)
		/* bailout */
		return;

	(void)ib_query_qp(cma_id->qp, &attr, ~0, &init_attr);
	iser_info("remote qpn:%x my qpn:%x\n", attr.dest_qp_num, cma_id->qp->qp_num);

	iser_conn->state = ISER_CONN_UP;
	complete(&iser_conn->up_completion);
}

static void iser_disconnected_handler(struct rdma_cm_id *cma_id)
{
	struct iser_conn *iser_conn = (struct iser_conn *)cma_id->context;

	if (iser_conn_terminate(iser_conn)) {
		if (iser_conn->iscsi_conn)
			iscsi_conn_failure(iser_conn->iscsi_conn,
					   ISCSI_ERR_CONN_FAILED);
		else
			iser_err("iscsi_iser connection isn't bound\n");
	}
}

static void iser_cleanup_handler(struct rdma_cm_id *cma_id,
				 bool destroy_device)
{
	struct iser_conn *iser_conn = (struct iser_conn *)cma_id->context;

	/*
	 * We are not guaranteed that we visited disconnected_handler
	 * by now, call it here to be safe that we handle CM drep
	 * and flush errors.
	 */
	iser_disconnected_handler(cma_id);
	iser_free_ib_conn_res(iser_conn, destroy_device);
	complete(&iser_conn->ib_completion);
};

static int iser_cma_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	struct iser_conn *iser_conn;
	int ret = 0;

	iser_conn = (struct iser_conn *)cma_id->context;
	iser_info("event %d status %d conn %p id %p\n",
		  event->event, event->status, cma_id->context, cma_id);

	mutex_lock(&iser_conn->state_mutex);
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
	case RDMA_CM_EVENT_ADDR_CHANGE:
	case RDMA_CM_EVENT_TIMEWAIT_EXIT:
		iser_cleanup_handler(cma_id, false);
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		/*
		 * we *must* destroy the device as we cannot rely
		 * on iscsid to be around to initiate error handling.
		 * also if we are not in state DOWN implicitly destroy
		 * the cma_id.
		 */
		iser_cleanup_handler(cma_id, true);
		if (iser_conn->state != ISER_CONN_DOWN) {
			iser_conn->ib_conn.cma_id = NULL;
			ret = 1;
		}
		break;
	default:
		iser_err("Unexpected RDMA CM event (%d)\n", event->event);
		break;
	}
	mutex_unlock(&iser_conn->state_mutex);

	return ret;
}

void iser_conn_init(struct iser_conn *iser_conn)
{
	iser_conn->state = ISER_CONN_INIT;
	iser_conn->ib_conn.post_recv_buf_count = 0;
	init_completion(&iser_conn->ib_conn.flush_comp);
	init_completion(&iser_conn->stop_completion);
	init_completion(&iser_conn->ib_completion);
	init_completion(&iser_conn->up_completion);
	INIT_LIST_HEAD(&iser_conn->conn_list);
	spin_lock_init(&iser_conn->ib_conn.lock);
	mutex_init(&iser_conn->state_mutex);
}

 /**
 * starts the process of connecting to the target
 * sleeps until the connection is established or rejected
 */
int iser_connect(struct iser_conn   *iser_conn,
		 struct sockaddr    *src_addr,
		 struct sockaddr    *dst_addr,
		 int                 non_blocking)
{
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	int err = 0;

	mutex_lock(&iser_conn->state_mutex);

	sprintf(iser_conn->name, "%pISp", dst_addr);

	iser_info("connecting to: %s\n", iser_conn->name);

	/* the device is known only --after-- address resolution */
	ib_conn->device = NULL;

	iser_conn->state = ISER_CONN_PENDING;

	ib_conn->beacon.wr_id = ISER_BEACON_WRID;
	ib_conn->beacon.opcode = IB_WR_SEND;

	ib_conn->cma_id = rdma_create_id(iser_cma_handler,
					 (void *)iser_conn,
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
		wait_for_completion_interruptible(&iser_conn->up_completion);

		if (iser_conn->state != ISER_CONN_UP) {
			err =  -EIO;
			goto connect_failure;
		}
	}
	mutex_unlock(&iser_conn->state_mutex);

	mutex_lock(&ig.connlist_mutex);
	list_add(&iser_conn->conn_list, &ig.connlist);
	mutex_unlock(&ig.connlist_mutex);
	return 0;

id_failure:
	ib_conn->cma_id = NULL;
addr_failure:
	iser_conn->state = ISER_CONN_DOWN;
connect_failure:
	mutex_unlock(&iser_conn->state_mutex);
	iser_conn_release(iser_conn);
	return err;
}

/**
 * iser_reg_page_vec - Register physical memory
 *
 * returns: 0 on success, errno code on failure
 */
int iser_reg_page_vec(struct ib_conn *ib_conn,
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
	struct iser_conn *iser_conn = iser_task->iser_conn;
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct fast_reg_descriptor *desc = reg->mem_h;

	if (!reg->is_mr)
		return;

	reg->mem_h = NULL;
	reg->is_mr = 0;
	spin_lock_bh(&ib_conn->lock);
	list_add_tail(&desc->list, &ib_conn->fastreg.pool);
	spin_unlock_bh(&ib_conn->lock);
}

int iser_post_recvl(struct iser_conn *iser_conn)
{
	struct ib_recv_wr rx_wr, *rx_wr_failed;
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	struct ib_sge	  sge;
	int ib_ret;

	sge.addr   = iser_conn->login_resp_dma;
	sge.length = ISER_RX_LOGIN_SIZE;
	sge.lkey   = ib_conn->device->mr->lkey;

	rx_wr.wr_id   = (unsigned long)iser_conn->login_resp_buf;
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

int iser_post_recvm(struct iser_conn *iser_conn, int count)
{
	struct ib_recv_wr *rx_wr, *rx_wr_failed;
	int i, ib_ret;
	struct ib_conn *ib_conn = &iser_conn->ib_conn;
	unsigned int my_rx_head = iser_conn->rx_desc_head;
	struct iser_rx_desc *rx_desc;

	for (rx_wr = ib_conn->rx_wr, i = 0; i < count; i++, rx_wr++) {
		rx_desc		= &iser_conn->rx_descs[my_rx_head];
		rx_wr->wr_id	= (unsigned long)rx_desc;
		rx_wr->sg_list	= &rx_desc->rx_sg;
		rx_wr->num_sge	= 1;
		rx_wr->next	= rx_wr + 1;
		my_rx_head = (my_rx_head + 1) & iser_conn->qp_max_recv_dtos_mask;
	}

	rx_wr--;
	rx_wr->next = NULL; /* mark end of work requests list */

	ib_conn->post_recv_buf_count += count;
	ib_ret	= ib_post_recv(ib_conn->qp, ib_conn->rx_wr, &rx_wr_failed);
	if (ib_ret) {
		iser_err("ib_post_recv failed ret=%d\n", ib_ret);
		ib_conn->post_recv_buf_count -= count;
	} else
		iser_conn->rx_desc_head = my_rx_head;
	return ib_ret;
}


/**
 * iser_start_send - Initiate a Send DTO operation
 *
 * returns 0 on success, -1 on failure
 */
int iser_post_send(struct ib_conn *ib_conn, struct iser_tx_desc *tx_desc,
		   bool signal)
{
	int		  ib_ret;
	struct ib_send_wr send_wr, *send_wr_failed;

	ib_dma_sync_single_for_device(ib_conn->device->ib_device,
				      tx_desc->dma_addr, ISER_HEADERS_LEN,
				      DMA_TO_DEVICE);

	send_wr.next	   = NULL;
	send_wr.wr_id	   = (unsigned long)tx_desc;
	send_wr.sg_list	   = tx_desc->tx_sg;
	send_wr.num_sge	   = tx_desc->num_sge;
	send_wr.opcode	   = IB_WR_SEND;
	send_wr.send_flags = signal ? IB_SEND_SIGNALED : 0;

	ib_ret = ib_post_send(ib_conn->qp, &send_wr, &send_wr_failed);
	if (ib_ret)
		iser_err("ib_post_send failed, ret:%d\n", ib_ret);

	return ib_ret;
}

/**
 * is_iser_tx_desc - Indicate if the completion wr_id
 *     is a TX descriptor or not.
 * @iser_conn: iser connection
 * @wr_id: completion WR identifier
 *
 * Since we cannot rely on wc opcode in FLUSH errors
 * we must work around it by checking if the wr_id address
 * falls in the iser connection rx_descs buffer. If so
 * it is an RX descriptor, otherwize it is a TX.
 */
static inline bool
is_iser_tx_desc(struct iser_conn *iser_conn, void *wr_id)
{
	void *start = iser_conn->rx_descs;
	int len = iser_conn->num_rx_descs * sizeof(*iser_conn->rx_descs);

	if (wr_id >= start && wr_id < start + len)
		return false;

	return true;
}

/**
 * iser_handle_comp_error() - Handle error completion
 * @ib_conn:   connection RDMA resources
 * @wc:        work completion
 *
 * Notes: We may handle a FLUSH error completion and in this case
 *        we only cleanup in case TX type was DATAOUT. For non-FLUSH
 *        error completion we should also notify iscsi layer that
 *        connection is failed (in case we passed bind stage).
 */
static void
iser_handle_comp_error(struct ib_conn *ib_conn,
		       struct ib_wc *wc)
{
	struct iser_conn *iser_conn = container_of(ib_conn, struct iser_conn,
						   ib_conn);

	if (wc->status != IB_WC_WR_FLUSH_ERR)
		if (iser_conn->iscsi_conn)
			iscsi_conn_failure(iser_conn->iscsi_conn,
					   ISCSI_ERR_CONN_FAILED);

	if (is_iser_tx_desc(iser_conn, (void *)wc->wr_id)) {
		struct iser_tx_desc *desc = (struct iser_tx_desc *)wc->wr_id;

		if (desc->type == ISCSI_TX_DATAOUT)
			kmem_cache_free(ig.desc_cache, desc);
	} else {
		ib_conn->post_recv_buf_count--;
	}
}

/**
 * iser_handle_wc - handle a single work completion
 * @wc: work completion
 *
 * Soft-IRQ context, work completion can be either
 * SEND or RECV, and can turn out successful or
 * with error (or flush error).
 */
static void iser_handle_wc(struct ib_wc *wc)
{
	struct ib_conn *ib_conn;
	struct iser_tx_desc *tx_desc;
	struct iser_rx_desc *rx_desc;

	ib_conn = wc->qp->qp_context;
	if (wc->status == IB_WC_SUCCESS) {
		if (wc->opcode == IB_WC_RECV) {
			rx_desc = (struct iser_rx_desc *)wc->wr_id;
			iser_rcv_completion(rx_desc, wc->byte_len,
					    ib_conn);
		} else
		if (wc->opcode == IB_WC_SEND) {
			tx_desc = (struct iser_tx_desc *)wc->wr_id;
			iser_snd_completion(tx_desc, ib_conn);
		} else {
			iser_err("Unknown wc opcode %d\n", wc->opcode);
		}
	} else {
		if (wc->status != IB_WC_WR_FLUSH_ERR)
			iser_err("wr id %llx status %d vend_err %x\n",
				 wc->wr_id, wc->status, wc->vendor_err);
		else
			iser_dbg("flush error: wr id %llx\n", wc->wr_id);

		if (wc->wr_id != ISER_FASTREG_LI_WRID &&
		    wc->wr_id != ISER_BEACON_WRID)
			iser_handle_comp_error(ib_conn, wc);

		/* complete in case all flush errors were consumed */
		if (wc->wr_id == ISER_BEACON_WRID)
			complete(&ib_conn->flush_comp);
	}
}

/**
 * iser_cq_tasklet_fn - iSER completion polling loop
 * @data: iSER completion context
 *
 * Soft-IRQ context, polling connection CQ until
 * either CQ was empty or we exausted polling budget
 */
static void iser_cq_tasklet_fn(unsigned long data)
{
	struct iser_comp *comp = (struct iser_comp *)data;
	struct ib_cq *cq = comp->cq;
	struct ib_wc *const wcs = comp->wcs;
	int i, n, completed = 0;

	while ((n = ib_poll_cq(cq, ARRAY_SIZE(comp->wcs), wcs)) > 0) {
		for (i = 0; i < n; i++)
			iser_handle_wc(&wcs[i]);

		completed += n;
		if (completed >= iser_cq_poll_limit)
			break;
	}

	/*
	 * It is assumed here that arming CQ only once its empty
	 * would not cause interrupts to be missed.
	 */
	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);

	iser_dbg("got %d completions\n", completed);
}

static void iser_cq_callback(struct ib_cq *cq, void *cq_context)
{
	struct iser_comp *comp = cq_context;

	tasklet_schedule(&comp->tasklet);
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
