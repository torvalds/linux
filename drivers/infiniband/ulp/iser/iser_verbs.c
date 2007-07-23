/*
 * Copyright (c) 2004, 2005, 2006 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems.  All rights reserved.
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
 *
 * $Id: iser_verbs.c 7051 2006-05-10 12:29:11Z ogerlitz $
 */
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/version.h>

#include "iscsi_iser.h"

#define ISCSI_ISER_MAX_CONN	8
#define ISER_MAX_CQ_LEN		((ISER_QP_MAX_RECV_DTOS + \
				ISER_QP_MAX_REQ_DTOS) *   \
				 ISCSI_ISER_MAX_CONN)

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

/**
 * iser_create_device_ib_res - creates Protection Domain (PD), Completion
 * Queue (CQ), DMA Memory Region (DMA MR) with the device associated with
 * the adapator.
 *
 * returns 0 on success, -1 on failure
 */
static int iser_create_device_ib_res(struct iser_device *device)
{
	device->pd = ib_alloc_pd(device->ib_device);
	if (IS_ERR(device->pd))
		goto pd_err;

	device->cq = ib_create_cq(device->ib_device,
				  iser_cq_callback,
				  iser_cq_event_callback,
				  (void *)device,
				  ISER_MAX_CQ_LEN, 0);
	if (IS_ERR(device->cq))
		goto cq_err;

	if (ib_req_notify_cq(device->cq, IB_CQ_NEXT_COMP))
		goto cq_arm_err;

	tasklet_init(&device->cq_tasklet,
		     iser_cq_tasklet_fn,
		     (unsigned long)device);

	device->mr = ib_get_dma_mr(device->pd, IB_ACCESS_LOCAL_WRITE |
				   IB_ACCESS_REMOTE_WRITE |
				   IB_ACCESS_REMOTE_READ);
	if (IS_ERR(device->mr))
		goto dma_mr_err;

	return 0;

dma_mr_err:
	tasklet_kill(&device->cq_tasklet);
cq_arm_err:
	ib_destroy_cq(device->cq);
cq_err:
	ib_dealloc_pd(device->pd);
pd_err:
	iser_err("failed to allocate an IB resource\n");
	return -1;
}

/**
 * iser_free_device_ib_res - destory/dealloc/dereg the DMA MR,
 * CQ and PD created with the device associated with the adapator.
 */
static void iser_free_device_ib_res(struct iser_device *device)
{
	BUG_ON(device->mr == NULL);

	tasklet_kill(&device->cq_tasklet);

	(void)ib_dereg_mr(device->mr);
	(void)ib_destroy_cq(device->cq);
	(void)ib_dealloc_pd(device->pd);

	device->mr = NULL;
	device->cq = NULL;
	device->pd = NULL;
}

/**
 * iser_create_ib_conn_res - Creates FMR pool and Queue-Pair (QP)
 *
 * returns 0 on success, -1 on failure
 */
static int iser_create_ib_conn_res(struct iser_conn *ib_conn)
{
	struct iser_device	*device;
	struct ib_qp_init_attr	init_attr;
	int			ret;
	struct ib_fmr_pool_param params;

	BUG_ON(ib_conn->device == NULL);

	device = ib_conn->device;

	ib_conn->page_vec = kmalloc(sizeof(struct iser_page_vec) +
				    (sizeof(u64) * (ISCSI_ISER_SG_TABLESIZE +1)),
				    GFP_KERNEL);
	if (!ib_conn->page_vec) {
		ret = -ENOMEM;
		goto alloc_err;
	}
	ib_conn->page_vec->pages = (u64 *) (ib_conn->page_vec + 1);

	params.page_shift        = SHIFT_4K;
	/* when the first/last SG element are not start/end *
	 * page aligned, the map whould be of N+1 pages     */
	params.max_pages_per_fmr = ISCSI_ISER_SG_TABLESIZE + 1;
	/* make the pool size twice the max number of SCSI commands *
	 * the ML is expected to queue, watermark for unmap at 50%  */
	params.pool_size	 = ISCSI_DEF_XMIT_CMDS_MAX * 2;
	params.dirty_watermark	 = ISCSI_DEF_XMIT_CMDS_MAX;
	params.cache		 = 0;
	params.flush_function	 = NULL;
	params.access		 = (IB_ACCESS_LOCAL_WRITE  |
				    IB_ACCESS_REMOTE_WRITE |
				    IB_ACCESS_REMOTE_READ);

	ib_conn->fmr_pool = ib_create_fmr_pool(device->pd, &params);
	if (IS_ERR(ib_conn->fmr_pool)) {
		ret = PTR_ERR(ib_conn->fmr_pool);
		goto fmr_pool_err;
	}

	memset(&init_attr, 0, sizeof init_attr);

	init_attr.event_handler = iser_qp_event_callback;
	init_attr.qp_context	= (void *)ib_conn;
	init_attr.send_cq	= device->cq;
	init_attr.recv_cq	= device->cq;
	init_attr.cap.max_send_wr  = ISER_QP_MAX_REQ_DTOS;
	init_attr.cap.max_recv_wr  = ISER_QP_MAX_RECV_DTOS;
	init_attr.cap.max_send_sge = MAX_REGD_BUF_VECTOR_LEN;
	init_attr.cap.max_recv_sge = 2;
	init_attr.sq_sig_type	= IB_SIGNAL_REQ_WR;
	init_attr.qp_type	= IB_QPT_RC;

	ret = rdma_create_qp(ib_conn->cma_id, device->pd, &init_attr);
	if (ret)
		goto qp_err;

	ib_conn->qp = ib_conn->cma_id->qp;
	iser_err("setting conn %p cma_id %p: fmr_pool %p qp %p\n",
		 ib_conn, ib_conn->cma_id,
		 ib_conn->fmr_pool, ib_conn->cma_id->qp);
	return ret;

qp_err:
	(void)ib_destroy_fmr_pool(ib_conn->fmr_pool);
fmr_pool_err:
	kfree(ib_conn->page_vec);
alloc_err:
	iser_err("unable to alloc mem or create resource, err %d\n", ret);
	return ret;
}

/**
 * releases the FMR pool, QP and CMA ID objects, returns 0 on success,
 * -1 on failure
 */
static int iser_free_ib_conn_res(struct iser_conn *ib_conn)
{
	BUG_ON(ib_conn == NULL);

	iser_err("freeing conn %p cma_id %p fmr pool %p qp %p\n",
		 ib_conn, ib_conn->cma_id,
		 ib_conn->fmr_pool, ib_conn->qp);

	/* qp is created only once both addr & route are resolved */
	if (ib_conn->fmr_pool != NULL)
		ib_destroy_fmr_pool(ib_conn->fmr_pool);

	if (ib_conn->qp != NULL)
		rdma_destroy_qp(ib_conn->cma_id);

	if (ib_conn->cma_id != NULL)
		rdma_destroy_id(ib_conn->cma_id);

	ib_conn->fmr_pool = NULL;
	ib_conn->qp	  = NULL;
	ib_conn->cma_id   = NULL;
	kfree(ib_conn->page_vec);

	return 0;
}

/**
 * based on the resolved device node GUID see if there already allocated
 * device for this device. If there's no such, create one.
 */
static
struct iser_device *iser_device_find_by_ib_device(struct rdma_cm_id *cma_id)
{
	struct list_head    *p_list;
	struct iser_device  *device = NULL;

	mutex_lock(&ig.device_list_mutex);

	p_list = ig.device_list.next;
	while (p_list != &ig.device_list) {
		device = list_entry(p_list, struct iser_device, ig_list);
		/* find if there's a match using the node GUID */
		if (device->ib_device->node_guid == cma_id->device->node_guid)
			break;
	}

	if (device == NULL) {
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
	}
out:
	BUG_ON(device == NULL);
	device->refcount++;
	mutex_unlock(&ig.device_list_mutex);
	return device;
}

/* if there's no demand for this device, release it */
static void iser_device_try_release(struct iser_device *device)
{
	mutex_lock(&ig.device_list_mutex);
	device->refcount--;
	iser_err("device %p refcount %d\n",device,device->refcount);
	if (!device->refcount) {
		iser_free_device_ib_res(device);
		list_del(&device->ig_list);
		kfree(device);
	}
	mutex_unlock(&ig.device_list_mutex);
}

int iser_conn_state_comp(struct iser_conn *ib_conn,
			enum iser_ib_conn_state comp)
{
	int ret;

	spin_lock_bh(&ib_conn->lock);
	ret = (ib_conn->state == comp);
	spin_unlock_bh(&ib_conn->lock);
	return ret;
}

static int iser_conn_state_comp_exch(struct iser_conn *ib_conn,
				     enum iser_ib_conn_state comp,
				     enum iser_ib_conn_state exch)
{
	int ret;

	spin_lock_bh(&ib_conn->lock);
	if ((ret = (ib_conn->state == comp)))
		ib_conn->state = exch;
	spin_unlock_bh(&ib_conn->lock);
	return ret;
}

/**
 * Frees all conn objects and deallocs conn descriptor
 */
static void iser_conn_release(struct iser_conn *ib_conn)
{
	struct iser_device  *device = ib_conn->device;

	BUG_ON(ib_conn->state != ISER_CONN_DOWN);

	mutex_lock(&ig.connlist_mutex);
	list_del(&ib_conn->conn_list);
	mutex_unlock(&ig.connlist_mutex);

	iser_free_ib_conn_res(ib_conn);
	ib_conn->device = NULL;
	/* on EVENT_ADDR_ERROR there's no device yet for this conn */
	if (device != NULL)
		iser_device_try_release(device);
	if (ib_conn->iser_conn)
		ib_conn->iser_conn->ib_conn = NULL;
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

	wait_event_interruptible(ib_conn->wait,
				 ib_conn->state == ISER_CONN_DOWN);

	iser_conn_release(ib_conn);
}

static void iser_connect_error(struct rdma_cm_id *cma_id)
{
	struct iser_conn *ib_conn;
	ib_conn = (struct iser_conn *)cma_id->context;

	ib_conn->state = ISER_CONN_DOWN;
	wake_up_interruptible(&ib_conn->wait);
}

static void iser_addr_handler(struct rdma_cm_id *cma_id)
{
	struct iser_device *device;
	struct iser_conn   *ib_conn;
	int    ret;

	device = iser_device_find_by_ib_device(cma_id);
	ib_conn = (struct iser_conn *)cma_id->context;
	ib_conn->device = device;

	ret = rdma_resolve_route(cma_id, 1000);
	if (ret) {
		iser_err("resolve route failed: %d\n", ret);
		iser_connect_error(cma_id);
	}
	return;
}

static void iser_route_handler(struct rdma_cm_id *cma_id)
{
	struct rdma_conn_param conn_param;
	int    ret;

	ret = iser_create_ib_conn_res((struct iser_conn *)cma_id->context);
	if (ret)
		goto failure;

	iser_dbg("path.mtu is %d setting it to %d\n",
		 cma_id->route.path_rec->mtu, IB_MTU_1024);

	/* we must set the MTU to 1024 as this is what the target is assuming */
	if (cma_id->route.path_rec->mtu > IB_MTU_1024)
		cma_id->route.path_rec->mtu = IB_MTU_1024;

	memset(&conn_param, 0, sizeof conn_param);
	conn_param.responder_resources = 4;
	conn_param.initiator_depth     = 1;
	conn_param.retry_count	       = 7;
	conn_param.rnr_retry_count     = 6;

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

	ib_conn = (struct iser_conn *)cma_id->context;
	ib_conn->state = ISER_CONN_UP;
	wake_up_interruptible(&ib_conn->wait);
}

static void iser_disconnected_handler(struct rdma_cm_id *cma_id)
{
	struct iser_conn *ib_conn;

	ib_conn = (struct iser_conn *)cma_id->context;
	ib_conn->disc_evt_flag = 1;

	/* getting here when the state is UP means that the conn is being *
	 * terminated asynchronously from the iSCSI layer's perspective.  */
	if (iser_conn_state_comp_exch(ib_conn, ISER_CONN_UP,
				      ISER_CONN_TERMINATING))
		iscsi_conn_failure(ib_conn->iser_conn->iscsi_conn,
				   ISCSI_ERR_CONN_FAILED);

	/* Complete the termination process if no posts are pending */
	if ((atomic_read(&ib_conn->post_recv_buf_count) == 0) &&
	    (atomic_read(&ib_conn->post_send_buf_count) == 0)) {
		ib_conn->state = ISER_CONN_DOWN;
		wake_up_interruptible(&ib_conn->wait);
	}
}

static int iser_cma_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	int ret = 0;

	iser_err("event %d conn %p id %p\n",event->event,cma_id->context,cma_id);

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
		iser_err("event: %d, error: %d\n", event->event, event->status);
		iser_connect_error(cma_id);
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
		iser_disconnected_handler(cma_id);
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
		BUG();
		break;
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
		BUG();
		break;
	case RDMA_CM_EVENT_CONNECT_REQUEST:
	default:
		break;
	}
	return ret;
}

int iser_conn_init(struct iser_conn **ibconn)
{
	struct iser_conn *ib_conn;

	ib_conn = kzalloc(sizeof *ib_conn, GFP_KERNEL);
	if (!ib_conn) {
		iser_err("can't alloc memory for struct iser_conn\n");
		return -ENOMEM;
	}
	ib_conn->state = ISER_CONN_INIT;
	init_waitqueue_head(&ib_conn->wait);
	atomic_set(&ib_conn->post_recv_buf_count, 0);
	atomic_set(&ib_conn->post_send_buf_count, 0);
	INIT_LIST_HEAD(&ib_conn->conn_list);
	spin_lock_init(&ib_conn->lock);

	*ibconn = ib_conn;
	return 0;
}

 /**
 * starts the process of connecting to the target
 * sleeps untill the connection is established or rejected
 */
int iser_connect(struct iser_conn   *ib_conn,
		 struct sockaddr_in *src_addr,
		 struct sockaddr_in *dst_addr,
		 int                 non_blocking)
{
	struct sockaddr *src, *dst;
	int err = 0;

	sprintf(ib_conn->name,"%d.%d.%d.%d:%d",
		NIPQUAD(dst_addr->sin_addr.s_addr), dst_addr->sin_port);

	/* the device is known only --after-- address resolution */
	ib_conn->device = NULL;

	iser_err("connecting to: %d.%d.%d.%d, port 0x%x\n",
		 NIPQUAD(dst_addr->sin_addr), dst_addr->sin_port);

	ib_conn->state = ISER_CONN_PENDING;

	ib_conn->cma_id = rdma_create_id(iser_cma_handler,
					     (void *)ib_conn,
					     RDMA_PS_TCP);
	if (IS_ERR(ib_conn->cma_id)) {
		err = PTR_ERR(ib_conn->cma_id);
		iser_err("rdma_create_id failed: %d\n", err);
		goto id_failure;
	}

	src = (struct sockaddr *)src_addr;
	dst = (struct sockaddr *)dst_addr;
	err = rdma_resolve_addr(ib_conn->cma_id, src, dst, 1000);
	if (err) {
		iser_err("rdma_resolve_addr failed: %d\n", err);
		goto addr_failure;
	}

	if (!non_blocking) {
		wait_event_interruptible(ib_conn->wait,
					 (ib_conn->state != ISER_CONN_PENDING));

		if (ib_conn->state != ISER_CONN_UP) {
			err =  -EIO;
			goto connect_failure;
		}
	}

	mutex_lock(&ig.connlist_mutex);
	list_add(&ib_conn->conn_list, &ig.connlist);
	mutex_unlock(&ig.connlist_mutex);
	return 0;

id_failure:
	ib_conn->cma_id = NULL;
addr_failure:
	ib_conn->state = ISER_CONN_DOWN;
connect_failure:
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

	mem  = ib_fmr_pool_map_phys(ib_conn->fmr_pool,
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
	mem_reg->is_fmr = 1;
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
 * Unregister (previosuly registered) memory.
 */
void iser_unreg_mem(struct iser_mem_reg *reg)
{
	int ret;

	iser_dbg("PHYSICAL Mem.Unregister mem_h %p\n",reg->mem_h);

	ret = ib_fmr_pool_unmap((struct ib_pool_fmr *)reg->mem_h);
	if (ret)
		iser_err("ib_fmr_pool_unmap failed %d\n", ret);

	reg->mem_h = NULL;
}

/**
 * iser_dto_to_iov - builds IOV from a dto descriptor
 */
static void iser_dto_to_iov(struct iser_dto *dto, struct ib_sge *iov, int iov_len)
{
	int		     i;
	struct ib_sge	     *sge;
	struct iser_regd_buf *regd_buf;

	if (dto->regd_vector_len > iov_len) {
		iser_err("iov size %d too small for posting dto of len %d\n",
			 iov_len, dto->regd_vector_len);
		BUG();
	}

	for (i = 0; i < dto->regd_vector_len; i++) {
		sge	    = &iov[i];
		regd_buf  = dto->regd[i];

		sge->addr   = regd_buf->reg.va;
		sge->length = regd_buf->reg.len;
		sge->lkey   = regd_buf->reg.lkey;

		if (dto->used_sz[i] > 0)  /* Adjust size */
			sge->length = dto->used_sz[i];

		/* offset and length should not exceed the regd buf length */
		if (sge->length + dto->offset[i] > regd_buf->reg.len) {
			iser_err("Used len:%ld + offset:%d, exceed reg.buf.len:"
				 "%ld in dto:0x%p [%d], va:0x%08lX\n",
				 (unsigned long)sge->length, dto->offset[i],
				 (unsigned long)regd_buf->reg.len, dto, i,
				 (unsigned long)sge->addr);
			BUG();
		}

		sge->addr += dto->offset[i]; /* Adjust offset */
	}
}

/**
 * iser_post_recv - Posts a receive buffer.
 *
 * returns 0 on success, -1 on failure
 */
int iser_post_recv(struct iser_desc *rx_desc)
{
	int		  ib_ret, ret_val = 0;
	struct ib_recv_wr recv_wr, *recv_wr_failed;
	struct ib_sge	  iov[2];
	struct iser_conn  *ib_conn;
	struct iser_dto   *recv_dto = &rx_desc->dto;

	/* Retrieve conn */
	ib_conn = recv_dto->ib_conn;

	iser_dto_to_iov(recv_dto, iov, 2);

	recv_wr.next	= NULL;
	recv_wr.sg_list = iov;
	recv_wr.num_sge = recv_dto->regd_vector_len;
	recv_wr.wr_id	= (unsigned long)rx_desc;

	atomic_inc(&ib_conn->post_recv_buf_count);
	ib_ret	= ib_post_recv(ib_conn->qp, &recv_wr, &recv_wr_failed);
	if (ib_ret) {
		iser_err("ib_post_recv failed ret=%d\n", ib_ret);
		atomic_dec(&ib_conn->post_recv_buf_count);
		ret_val = -1;
	}

	return ret_val;
}

/**
 * iser_start_send - Initiate a Send DTO operation
 *
 * returns 0 on success, -1 on failure
 */
int iser_post_send(struct iser_desc *tx_desc)
{
	int		  ib_ret, ret_val = 0;
	struct ib_send_wr send_wr, *send_wr_failed;
	struct ib_sge	  iov[MAX_REGD_BUF_VECTOR_LEN];
	struct iser_conn  *ib_conn;
	struct iser_dto   *dto = &tx_desc->dto;

	ib_conn = dto->ib_conn;

	iser_dto_to_iov(dto, iov, MAX_REGD_BUF_VECTOR_LEN);

	send_wr.next	   = NULL;
	send_wr.wr_id	   = (unsigned long)tx_desc;
	send_wr.sg_list	   = iov;
	send_wr.num_sge	   = dto->regd_vector_len;
	send_wr.opcode	   = IB_WR_SEND;
	send_wr.send_flags = dto->notify_enable ? IB_SEND_SIGNALED : 0;

	atomic_inc(&ib_conn->post_send_buf_count);

	ib_ret = ib_post_send(ib_conn->qp, &send_wr, &send_wr_failed);
	if (ib_ret) {
		iser_err("Failed to start SEND DTO, dto: 0x%p, IOV len: %d\n",
			 dto, dto->regd_vector_len);
		iser_err("ib_post_send failed, ret:%d\n", ib_ret);
		atomic_dec(&ib_conn->post_send_buf_count);
		ret_val = -1;
	}

	return ret_val;
}

static void iser_handle_comp_error(struct iser_desc *desc)
{
	struct iser_dto  *dto     = &desc->dto;
	struct iser_conn *ib_conn = dto->ib_conn;

	iser_dto_buffs_release(dto);

	if (desc->type == ISCSI_RX) {
		kfree(desc->data);
		kmem_cache_free(ig.desc_cache, desc);
		atomic_dec(&ib_conn->post_recv_buf_count);
	} else { /* type is TX control/command/dataout */
		if (desc->type == ISCSI_TX_DATAOUT)
			kmem_cache_free(ig.desc_cache, desc);
		atomic_dec(&ib_conn->post_send_buf_count);
	}

	if (atomic_read(&ib_conn->post_recv_buf_count) == 0 &&
	    atomic_read(&ib_conn->post_send_buf_count) == 0) {
		/* getting here when the state is UP means that the conn is *
		 * being terminated asynchronously from the iSCSI layer's   *
		 * perspective.                                             */
		if (iser_conn_state_comp_exch(ib_conn, ISER_CONN_UP,
		    ISER_CONN_TERMINATING))
			iscsi_conn_failure(ib_conn->iser_conn->iscsi_conn,
					   ISCSI_ERR_CONN_FAILED);

		/* complete the termination process if disconnect event was delivered *
		 * note there are no more non completed posts to the QP               */
		if (ib_conn->disc_evt_flag) {
			ib_conn->state = ISER_CONN_DOWN;
			wake_up_interruptible(&ib_conn->wait);
		}
	}
}

static void iser_cq_tasklet_fn(unsigned long data)
{
	 struct iser_device  *device = (struct iser_device *)data;
	 struct ib_cq	     *cq = device->cq;
	 struct ib_wc	     wc;
	 struct iser_desc    *desc;
	 unsigned long	     xfer_len;

	while (ib_poll_cq(cq, 1, &wc) == 1) {
		desc	 = (struct iser_desc *) (unsigned long) wc.wr_id;
		BUG_ON(desc == NULL);

		if (wc.status == IB_WC_SUCCESS) {
			if (desc->type == ISCSI_RX) {
				xfer_len = (unsigned long)wc.byte_len;
				iser_rcv_completion(desc, xfer_len);
			} else /* type == ISCSI_TX_CONTROL/SCSI_CMD/DOUT */
				iser_snd_completion(desc);
		} else {
			iser_err("comp w. error op %d status %d\n",desc->type,wc.status);
			iser_handle_comp_error(desc);
		}
	}
	/* #warning "it is assumed here that arming CQ only once its empty" *
	 * " would not cause interrupts to be missed"                       */
	ib_req_notify_cq(cq, IB_CQ_NEXT_COMP);
}

static void iser_cq_callback(struct ib_cq *cq, void *cq_context)
{
	struct iser_device  *device = (struct iser_device *)cq_context;

	tasklet_schedule(&device->cq_tasklet);
}
