// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"

void mana_ib_uncfg_vport(struct mana_ib_dev *dev, struct mana_ib_pd *pd,
			 u32 port)
{
	struct mana_port_context *mpc;
	struct net_device *ndev;

	ndev = mana_ib_get_netdev(&dev->ib_dev, port);
	mpc = netdev_priv(ndev);

	mutex_lock(&pd->vport_mutex);

	pd->vport_use_count--;
	WARN_ON(pd->vport_use_count < 0);

	if (!pd->vport_use_count)
		mana_uncfg_vport(mpc);

	mutex_unlock(&pd->vport_mutex);
}

int mana_ib_cfg_vport(struct mana_ib_dev *dev, u32 port, struct mana_ib_pd *pd,
		      u32 doorbell_id)
{
	struct mana_port_context *mpc;
	struct net_device *ndev;
	int err;

	ndev = mana_ib_get_netdev(&dev->ib_dev, port);
	mpc = netdev_priv(ndev);

	mutex_lock(&pd->vport_mutex);

	pd->vport_use_count++;
	if (pd->vport_use_count > 1) {
		ibdev_dbg(&dev->ib_dev,
			  "Skip as this PD is already configured vport\n");
		mutex_unlock(&pd->vport_mutex);
		return 0;
	}

	err = mana_cfg_vport(mpc, pd->pdn, doorbell_id);
	if (err) {
		pd->vport_use_count--;
		mutex_unlock(&pd->vport_mutex);

		ibdev_dbg(&dev->ib_dev, "Failed to configure vPort %d\n", err);
		return err;
	}

	mutex_unlock(&pd->vport_mutex);

	pd->tx_shortform_allowed = mpc->tx_shortform_allowed;
	pd->tx_vp_offset = mpc->tx_vp_offset;

	ibdev_dbg(&dev->ib_dev, "vport handle %llx pdid %x doorbell_id %x\n",
		  mpc->port_handle, pd->pdn, doorbell_id);

	return 0;
}

int mana_ib_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct mana_ib_pd *pd = container_of(ibpd, struct mana_ib_pd, ibpd);
	struct ib_device *ibdev = ibpd->device;
	struct gdma_create_pd_resp resp = {};
	struct gdma_create_pd_req req = {};
	enum gdma_pd_flags flags = 0;
	struct mana_ib_dev *dev;
	struct gdma_context *gc;
	int err;

	dev = container_of(ibdev, struct mana_ib_dev, ib_dev);
	gc = mdev_to_gc(dev);

	mana_gd_init_req_hdr(&req.hdr, GDMA_CREATE_PD, sizeof(req),
			     sizeof(resp));

	if (!udata)
		flags |= GDMA_PD_FLAG_ALLOW_GPA_MR;

	req.flags = flags;
	err = mana_gd_send_request(gc, sizeof(req), &req,
				   sizeof(resp), &resp);

	if (err || resp.hdr.status) {
		ibdev_dbg(&dev->ib_dev,
			  "Failed to get pd_id err %d status %u\n", err,
			  resp.hdr.status);
		if (!err)
			err = -EPROTO;

		return err;
	}

	pd->pd_handle = resp.pd_handle;
	pd->pdn = resp.pd_id;
	ibdev_dbg(&dev->ib_dev, "pd_handle 0x%llx pd_id %d\n",
		  pd->pd_handle, pd->pdn);

	mutex_init(&pd->vport_mutex);
	pd->vport_use_count = 0;
	return 0;
}

int mana_ib_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct mana_ib_pd *pd = container_of(ibpd, struct mana_ib_pd, ibpd);
	struct ib_device *ibdev = ibpd->device;
	struct gdma_destory_pd_resp resp = {};
	struct gdma_destroy_pd_req req = {};
	struct mana_ib_dev *dev;
	struct gdma_context *gc;
	int err;

	dev = container_of(ibdev, struct mana_ib_dev, ib_dev);
	gc = mdev_to_gc(dev);

	mana_gd_init_req_hdr(&req.hdr, GDMA_DESTROY_PD, sizeof(req),
			     sizeof(resp));

	req.pd_handle = pd->pd_handle;
	err = mana_gd_send_request(gc, sizeof(req), &req,
				   sizeof(resp), &resp);

	if (err || resp.hdr.status) {
		ibdev_dbg(&dev->ib_dev,
			  "Failed to destroy pd_handle 0x%llx err %d status %u",
			  pd->pd_handle, err, resp.hdr.status);
		if (!err)
			err = -EPROTO;
	}

	return err;
}

static int mana_gd_destroy_doorbell_page(struct gdma_context *gc,
					 int doorbell_page)
{
	struct gdma_destroy_resource_range_req req = {};
	struct gdma_resp_hdr resp = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, GDMA_DESTROY_RESOURCE_RANGE,
			     sizeof(req), sizeof(resp));

	req.resource_type = GDMA_RESOURCE_DOORBELL_PAGE;
	req.num_resources = 1;
	req.allocated_resources = doorbell_page;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.status) {
		dev_err(gc->dev,
			"Failed to destroy doorbell page: ret %d, 0x%x\n",
			err, resp.status);
		return err ?: -EPROTO;
	}

	return 0;
}

static int mana_gd_allocate_doorbell_page(struct gdma_context *gc,
					  int *doorbell_page)
{
	struct gdma_allocate_resource_range_req req = {};
	struct gdma_allocate_resource_range_resp resp = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, GDMA_ALLOCATE_RESOURCE_RANGE,
			     sizeof(req), sizeof(resp));

	req.resource_type = GDMA_RESOURCE_DOORBELL_PAGE;
	req.num_resources = 1;
	req.alignment = PAGE_SIZE / MANA_PAGE_SIZE;

	/* Have GDMA start searching from 0 */
	req.allocated_resources = 0;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err || resp.hdr.status) {
		dev_err(gc->dev,
			"Failed to allocate doorbell page: ret %d, 0x%x\n",
			err, resp.hdr.status);
		return err ?: -EPROTO;
	}

	*doorbell_page = resp.allocated_resources;

	return 0;
}

int mana_ib_alloc_ucontext(struct ib_ucontext *ibcontext,
			   struct ib_udata *udata)
{
	struct mana_ib_ucontext *ucontext =
		container_of(ibcontext, struct mana_ib_ucontext, ibucontext);
	struct ib_device *ibdev = ibcontext->device;
	struct mana_ib_dev *mdev;
	struct gdma_context *gc;
	int doorbell_page;
	int ret;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);
	gc = mdev_to_gc(mdev);

	/* Allocate a doorbell page index */
	ret = mana_gd_allocate_doorbell_page(gc, &doorbell_page);
	if (ret) {
		ibdev_dbg(ibdev, "Failed to allocate doorbell page %d\n", ret);
		return ret;
	}

	ibdev_dbg(ibdev, "Doorbell page allocated %d\n", doorbell_page);

	ucontext->doorbell = doorbell_page;

	return 0;
}

void mana_ib_dealloc_ucontext(struct ib_ucontext *ibcontext)
{
	struct mana_ib_ucontext *mana_ucontext =
		container_of(ibcontext, struct mana_ib_ucontext, ibucontext);
	struct ib_device *ibdev = ibcontext->device;
	struct mana_ib_dev *mdev;
	struct gdma_context *gc;
	int ret;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);
	gc = mdev_to_gc(mdev);

	ret = mana_gd_destroy_doorbell_page(gc, mana_ucontext->doorbell);
	if (ret)
		ibdev_dbg(ibdev, "Failed to destroy doorbell page %d\n", ret);
}

int mana_ib_create_kernel_queue(struct mana_ib_dev *mdev, u32 size, enum gdma_queue_type type,
				struct mana_ib_queue *queue)
{
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct gdma_queue_spec spec = {};
	int err;

	queue->id = INVALID_QUEUE_ID;
	queue->gdma_region = GDMA_INVALID_DMA_REGION;
	spec.type = type;
	spec.monitor_avl_buf = false;
	spec.queue_size = size;
	err = mana_gd_create_mana_wq_cq(&gc->mana_ib, &spec, &queue->kmem);
	if (err)
		return err;
	/* take ownership into mana_ib from mana */
	queue->gdma_region = queue->kmem->mem_info.dma_region_handle;
	queue->kmem->mem_info.dma_region_handle = GDMA_INVALID_DMA_REGION;
	return 0;
}

int mana_ib_create_queue(struct mana_ib_dev *mdev, u64 addr, u32 size,
			 struct mana_ib_queue *queue)
{
	struct ib_umem *umem;
	int err;

	queue->umem = NULL;
	queue->id = INVALID_QUEUE_ID;
	queue->gdma_region = GDMA_INVALID_DMA_REGION;

	umem = ib_umem_get(&mdev->ib_dev, addr, size, IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(umem)) {
		err = PTR_ERR(umem);
		ibdev_dbg(&mdev->ib_dev, "Failed to get umem, %d\n", err);
		return err;
	}

	err = mana_ib_create_zero_offset_dma_region(mdev, umem, &queue->gdma_region);
	if (err) {
		ibdev_dbg(&mdev->ib_dev, "Failed to create dma region, %d\n", err);
		goto free_umem;
	}
	queue->umem = umem;

	ibdev_dbg(&mdev->ib_dev, "created dma region 0x%llx\n", queue->gdma_region);

	return 0;
free_umem:
	ib_umem_release(umem);
	return err;
}

void mana_ib_destroy_queue(struct mana_ib_dev *mdev, struct mana_ib_queue *queue)
{
	/* Ignore return code as there is not much we can do about it.
	 * The error message is printed inside.
	 */
	mana_ib_gd_destroy_dma_region(mdev, queue->gdma_region);
	ib_umem_release(queue->umem);
	if (queue->kmem)
		mana_gd_destroy_queue(mdev_to_gc(mdev), queue->kmem);
}

static int
mana_ib_gd_first_dma_region(struct mana_ib_dev *dev,
			    struct gdma_context *gc,
			    struct gdma_create_dma_region_req *create_req,
			    size_t num_pages, mana_handle_t *gdma_region,
			    u32 expected_status)
{
	struct gdma_create_dma_region_resp create_resp = {};
	unsigned int create_req_msg_size;
	int err;

	create_req_msg_size =
		struct_size(create_req, page_addr_list, num_pages);
	create_req->page_addr_list_len = num_pages;

	err = mana_gd_send_request(gc, create_req_msg_size, create_req,
				   sizeof(create_resp), &create_resp);
	if (err || create_resp.hdr.status != expected_status) {
		ibdev_dbg(&dev->ib_dev,
			  "Failed to create DMA region: %d, 0x%x\n",
			  err, create_resp.hdr.status);
		if (!err)
			err = -EPROTO;

		return err;
	}

	*gdma_region = create_resp.dma_region_handle;
	ibdev_dbg(&dev->ib_dev, "Created DMA region handle 0x%llx\n",
		  *gdma_region);

	return 0;
}

static int
mana_ib_gd_add_dma_region(struct mana_ib_dev *dev, struct gdma_context *gc,
			  struct gdma_dma_region_add_pages_req *add_req,
			  unsigned int num_pages, u32 expected_status)
{
	unsigned int add_req_msg_size =
		struct_size(add_req, page_addr_list, num_pages);
	struct gdma_general_resp add_resp = {};
	int err;

	mana_gd_init_req_hdr(&add_req->hdr, GDMA_DMA_REGION_ADD_PAGES,
			     add_req_msg_size, sizeof(add_resp));
	add_req->page_addr_list_len = num_pages;

	err = mana_gd_send_request(gc, add_req_msg_size, add_req,
				   sizeof(add_resp), &add_resp);
	if (err || add_resp.hdr.status != expected_status) {
		ibdev_dbg(&dev->ib_dev,
			  "Failed to create DMA region: %d, 0x%x\n",
			  err, add_resp.hdr.status);

		if (!err)
			err = -EPROTO;

		return err;
	}

	return 0;
}

static int mana_ib_gd_create_dma_region(struct mana_ib_dev *dev, struct ib_umem *umem,
					mana_handle_t *gdma_region, unsigned long page_sz)
{
	struct gdma_dma_region_add_pages_req *add_req = NULL;
	size_t num_pages_processed = 0, num_pages_to_handle;
	struct gdma_create_dma_region_req *create_req;
	unsigned int create_req_msg_size;
	struct hw_channel_context *hwc;
	struct ib_block_iter biter;
	size_t max_pgs_add_cmd = 0;
	size_t max_pgs_create_cmd;
	struct gdma_context *gc;
	size_t num_pages_total;
	unsigned int tail = 0;
	u64 *page_addr_list;
	void *request_buf;
	int err = 0;

	gc = mdev_to_gc(dev);
	hwc = gc->hwc.driver_data;

	num_pages_total = ib_umem_num_dma_blocks(umem, page_sz);

	max_pgs_create_cmd =
		(hwc->max_req_msg_size - sizeof(*create_req)) / sizeof(u64);
	num_pages_to_handle =
		min_t(size_t, num_pages_total, max_pgs_create_cmd);
	create_req_msg_size =
		struct_size(create_req, page_addr_list, num_pages_to_handle);

	request_buf = kzalloc(hwc->max_req_msg_size, GFP_KERNEL);
	if (!request_buf)
		return -ENOMEM;

	create_req = request_buf;
	mana_gd_init_req_hdr(&create_req->hdr, GDMA_CREATE_DMA_REGION,
			     create_req_msg_size,
			     sizeof(struct gdma_create_dma_region_resp));

	create_req->length = umem->length;
	create_req->offset_in_page = ib_umem_dma_offset(umem, page_sz);
	create_req->gdma_page_type = order_base_2(page_sz) - MANA_PAGE_SHIFT;
	create_req->page_count = num_pages_total;

	ibdev_dbg(&dev->ib_dev, "size_dma_region %lu num_pages_total %lu\n",
		  umem->length, num_pages_total);

	ibdev_dbg(&dev->ib_dev, "page_sz %lu offset_in_page %u\n",
		  page_sz, create_req->offset_in_page);

	ibdev_dbg(&dev->ib_dev, "num_pages_to_handle %lu, gdma_page_type %u",
		  num_pages_to_handle, create_req->gdma_page_type);

	page_addr_list = create_req->page_addr_list;
	rdma_umem_for_each_dma_block(umem, &biter, page_sz) {
		u32 expected_status = 0;

		page_addr_list[tail++] = rdma_block_iter_dma_address(&biter);
		if (tail < num_pages_to_handle)
			continue;

		if (num_pages_processed + num_pages_to_handle <
		    num_pages_total)
			expected_status = GDMA_STATUS_MORE_ENTRIES;

		if (!num_pages_processed) {
			/* First create message */
			err = mana_ib_gd_first_dma_region(dev, gc, create_req,
							  tail, gdma_region,
							  expected_status);
			if (err)
				goto out;

			max_pgs_add_cmd = (hwc->max_req_msg_size -
				sizeof(*add_req)) / sizeof(u64);

			add_req = request_buf;
			add_req->dma_region_handle = *gdma_region;
			add_req->reserved3 = 0;
			page_addr_list = add_req->page_addr_list;
		} else {
			/* Subsequent create messages */
			err = mana_ib_gd_add_dma_region(dev, gc, add_req, tail,
							expected_status);
			if (err)
				break;
		}

		num_pages_processed += tail;
		tail = 0;

		/* The remaining pages to create */
		num_pages_to_handle =
			min_t(size_t,
			      num_pages_total - num_pages_processed,
			      max_pgs_add_cmd);
	}

	if (err)
		mana_ib_gd_destroy_dma_region(dev, *gdma_region);

out:
	kfree(request_buf);
	return err;
}

int mana_ib_create_dma_region(struct mana_ib_dev *dev, struct ib_umem *umem,
			      mana_handle_t *gdma_region, u64 virt)
{
	unsigned long page_sz;

	page_sz = ib_umem_find_best_pgsz(umem, PAGE_SZ_BM, virt);
	if (!page_sz) {
		ibdev_dbg(&dev->ib_dev, "Failed to find page size.\n");
		return -EINVAL;
	}

	return mana_ib_gd_create_dma_region(dev, umem, gdma_region, page_sz);
}

int mana_ib_create_zero_offset_dma_region(struct mana_ib_dev *dev, struct ib_umem *umem,
					  mana_handle_t *gdma_region)
{
	unsigned long page_sz;

	/* Hardware requires dma region to align to chosen page size */
	page_sz = ib_umem_find_best_pgoff(umem, PAGE_SZ_BM, 0);
	if (!page_sz) {
		ibdev_dbg(&dev->ib_dev, "Failed to find page size.\n");
		return -EINVAL;
	}

	return mana_ib_gd_create_dma_region(dev, umem, gdma_region, page_sz);
}

int mana_ib_gd_destroy_dma_region(struct mana_ib_dev *dev, u64 gdma_region)
{
	struct gdma_context *gc = mdev_to_gc(dev);

	ibdev_dbg(&dev->ib_dev, "destroy dma region 0x%llx\n", gdma_region);

	return mana_gd_destroy_dma_region(gc, gdma_region);
}

int mana_ib_mmap(struct ib_ucontext *ibcontext, struct vm_area_struct *vma)
{
	struct mana_ib_ucontext *mana_ucontext =
		container_of(ibcontext, struct mana_ib_ucontext, ibucontext);
	struct ib_device *ibdev = ibcontext->device;
	struct mana_ib_dev *mdev;
	struct gdma_context *gc;
	phys_addr_t pfn;
	pgprot_t prot;
	int ret;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);
	gc = mdev_to_gc(mdev);

	if (vma->vm_pgoff != 0) {
		ibdev_dbg(ibdev, "Unexpected vm_pgoff %lu\n", vma->vm_pgoff);
		return -EINVAL;
	}

	/* Map to the page indexed by ucontext->doorbell */
	pfn = (gc->phys_db_page_base +
	       gc->db_page_size * mana_ucontext->doorbell) >>
	      PAGE_SHIFT;
	prot = pgprot_writecombine(vma->vm_page_prot);

	ret = rdma_user_mmap_io(ibcontext, vma, pfn, PAGE_SIZE, prot,
				NULL);
	if (ret)
		ibdev_dbg(ibdev, "can't rdma_user_mmap_io ret %d\n", ret);
	else
		ibdev_dbg(ibdev, "mapped I/O pfn 0x%llx page_size %lu, ret %d\n",
			  pfn, PAGE_SIZE, ret);

	return ret;
}

int mana_ib_get_port_immutable(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	immutable->core_cap_flags = RDMA_CORE_PORT_RAW_PACKET;
	if (port_num == 1) {
		immutable->core_cap_flags |= RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;
		immutable->max_mad_size = IB_MGMT_MAD_SIZE;
	}

	return 0;
}

int mana_ib_query_device(struct ib_device *ibdev, struct ib_device_attr *props,
			 struct ib_udata *uhw)
{
	struct mana_ib_dev *dev = container_of(ibdev,
			struct mana_ib_dev, ib_dev);

	memset(props, 0, sizeof(*props));
	props->max_mr_size = MANA_IB_MAX_MR_SIZE;
	props->page_size_cap = PAGE_SZ_BM;
	props->max_qp = dev->adapter_caps.max_qp_count;
	props->max_qp_wr = dev->adapter_caps.max_qp_wr;
	props->device_cap_flags = IB_DEVICE_RC_RNR_NAK_GEN;
	props->max_send_sge = dev->adapter_caps.max_send_sge_count;
	props->max_recv_sge = dev->adapter_caps.max_recv_sge_count;
	props->max_sge_rd = dev->adapter_caps.max_recv_sge_count;
	props->max_cq = dev->adapter_caps.max_cq_count;
	props->max_cqe = dev->adapter_caps.max_qp_wr;
	props->max_mr = dev->adapter_caps.max_mr_count;
	props->max_pd = dev->adapter_caps.max_pd_count;
	props->max_qp_rd_atom = dev->adapter_caps.max_inbound_read_limit;
	props->max_res_rd_atom = props->max_qp_rd_atom * props->max_qp;
	props->max_qp_init_rd_atom = dev->adapter_caps.max_outbound_read_limit;
	props->atomic_cap = IB_ATOMIC_NONE;
	props->masked_atomic_cap = IB_ATOMIC_NONE;
	props->max_ah = INT_MAX;
	props->max_pkeys = 1;
	props->local_ca_ack_delay = MANA_CA_ACK_DELAY;

	return 0;
}

int mana_ib_query_port(struct ib_device *ibdev, u32 port,
		       struct ib_port_attr *props)
{
	struct net_device *ndev = mana_ib_get_netdev(ibdev, port);

	if (!ndev)
		return -EINVAL;

	memset(props, 0, sizeof(*props));
	props->max_mtu = IB_MTU_4096;
	props->active_mtu = ib_mtu_int_to_enum(ndev->mtu);

	if (netif_carrier_ok(ndev) && netif_running(ndev)) {
		props->state = IB_PORT_ACTIVE;
		props->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	} else {
		props->state = IB_PORT_DOWN;
		props->phys_state = IB_PORT_PHYS_STATE_DISABLED;
	}

	props->active_width = IB_WIDTH_4X;
	props->active_speed = IB_SPEED_EDR;
	props->pkey_tbl_len = 1;
	if (port == 1) {
		props->gid_tbl_len = 16;
		props->port_cap_flags = IB_PORT_CM_SUP;
		props->ip_gids = true;
	}

	return 0;
}

enum rdma_link_layer mana_ib_get_link_layer(struct ib_device *device, u32 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

int mana_ib_query_pkey(struct ib_device *ibdev, u32 port, u16 index, u16 *pkey)
{
	if (index != 0)
		return -EINVAL;
	*pkey = IB_DEFAULT_PKEY_FULL;
	return 0;
}

int mana_ib_query_gid(struct ib_device *ibdev, u32 port, int index,
		      union ib_gid *gid)
{
	/* This version doesn't return GID properties */
	return 0;
}

void mana_ib_disassociate_ucontext(struct ib_ucontext *ibcontext)
{
}

int mana_ib_gd_query_adapter_caps(struct mana_ib_dev *dev)
{
	struct mana_ib_adapter_caps *caps = &dev->adapter_caps;
	struct mana_ib_query_adapter_caps_resp resp = {};
	struct mana_ib_query_adapter_caps_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_GET_ADAPTER_CAP, sizeof(req),
			     sizeof(resp));
	req.hdr.resp.msg_version = GDMA_MESSAGE_V4;
	req.hdr.dev_id = dev->gdma_dev->dev_id;

	err = mana_gd_send_request(mdev_to_gc(dev), sizeof(req),
				   &req, sizeof(resp), &resp);

	if (err) {
		ibdev_err(&dev->ib_dev,
			  "Failed to query adapter caps err %d", err);
		return err;
	}

	caps->max_sq_id = resp.max_sq_id;
	caps->max_rq_id = resp.max_rq_id;
	caps->max_cq_id = resp.max_cq_id;
	caps->max_qp_count = resp.max_qp_count;
	caps->max_cq_count = resp.max_cq_count;
	caps->max_mr_count = resp.max_mr_count;
	caps->max_pd_count = resp.max_pd_count;
	caps->max_inbound_read_limit = resp.max_inbound_read_limit;
	caps->max_outbound_read_limit = resp.max_outbound_read_limit;
	caps->mw_count = resp.mw_count;
	caps->max_srq_count = resp.max_srq_count;
	caps->max_qp_wr = min_t(u32,
				resp.max_requester_sq_size / GDMA_MAX_SQE_SIZE,
				resp.max_requester_rq_size / GDMA_MAX_RQE_SIZE);
	caps->max_inline_data_size = resp.max_inline_data_size;
	caps->max_send_sge_count = resp.max_send_sge_count;
	caps->max_recv_sge_count = resp.max_recv_sge_count;
	caps->feature_flags = resp.feature_flags;

	return 0;
}

static void
mana_ib_event_handler(void *ctx, struct gdma_queue *q, struct gdma_event *event)
{
	struct mana_ib_dev *mdev = (struct mana_ib_dev *)ctx;
	struct mana_ib_qp *qp;
	struct ib_event ev;
	u32 qpn;

	switch (event->type) {
	case GDMA_EQE_RNIC_QP_FATAL:
		qpn = event->details[0];
		qp = mana_get_qp_ref(mdev, qpn, false);
		if (!qp)
			break;
		if (qp->ibqp.event_handler) {
			ev.device = qp->ibqp.device;
			ev.element.qp = &qp->ibqp;
			ev.event = IB_EVENT_QP_FATAL;
			qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
		}
		mana_put_qp_ref(qp);
		break;
	default:
		break;
	}
}

int mana_ib_create_eqs(struct mana_ib_dev *mdev)
{
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct gdma_queue_spec spec = {};
	int err, i;

	spec.type = GDMA_EQ;
	spec.monitor_avl_buf = false;
	spec.queue_size = EQ_SIZE;
	spec.eq.callback = mana_ib_event_handler;
	spec.eq.context = mdev;
	spec.eq.log2_throttle_limit = LOG2_EQ_THROTTLE;
	spec.eq.msix_index = 0;

	err = mana_gd_create_mana_eq(&gc->mana_ib, &spec, &mdev->fatal_err_eq);
	if (err)
		return err;

	mdev->eqs = kcalloc(mdev->ib_dev.num_comp_vectors, sizeof(struct gdma_queue *),
			    GFP_KERNEL);
	if (!mdev->eqs) {
		err = -ENOMEM;
		goto destroy_fatal_eq;
	}
	spec.eq.callback = NULL;
	for (i = 0; i < mdev->ib_dev.num_comp_vectors; i++) {
		spec.eq.msix_index = (i + 1) % gc->num_msix_usable;
		err = mana_gd_create_mana_eq(mdev->gdma_dev, &spec, &mdev->eqs[i]);
		if (err)
			goto destroy_eqs;
	}

	return 0;

destroy_eqs:
	while (i-- > 0)
		mana_gd_destroy_queue(gc, mdev->eqs[i]);
	kfree(mdev->eqs);
destroy_fatal_eq:
	mana_gd_destroy_queue(gc, mdev->fatal_err_eq);
	return err;
}

void mana_ib_destroy_eqs(struct mana_ib_dev *mdev)
{
	struct gdma_context *gc = mdev_to_gc(mdev);
	int i;

	mana_gd_destroy_queue(gc, mdev->fatal_err_eq);

	for (i = 0; i < mdev->ib_dev.num_comp_vectors; i++)
		mana_gd_destroy_queue(gc, mdev->eqs[i]);

	kfree(mdev->eqs);
}

int mana_ib_gd_create_rnic_adapter(struct mana_ib_dev *mdev)
{
	struct mana_rnic_create_adapter_resp resp = {};
	struct mana_rnic_create_adapter_req req = {};
	struct gdma_context *gc = mdev_to_gc(mdev);
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_CREATE_ADAPTER, sizeof(req), sizeof(resp));
	req.hdr.req.msg_version = GDMA_MESSAGE_V2;
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.notify_eq_id = mdev->fatal_err_eq->id;

	if (mdev->adapter_caps.feature_flags & MANA_IB_FEATURE_CLIENT_ERROR_CQE_SUPPORT)
		req.feature_flags |= MANA_IB_FEATURE_CLIENT_ERROR_CQE_REQUEST;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to create RNIC adapter err %d", err);
		return err;
	}
	mdev->adapter_handle = resp.adapter;

	return 0;
}

int mana_ib_gd_destroy_rnic_adapter(struct mana_ib_dev *mdev)
{
	struct mana_rnic_destroy_adapter_resp resp = {};
	struct mana_rnic_destroy_adapter_req req = {};
	struct gdma_context *gc;
	int err;

	gc = mdev_to_gc(mdev);
	mana_gd_init_req_hdr(&req.hdr, MANA_IB_DESTROY_ADAPTER, sizeof(req), sizeof(resp));
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.adapter = mdev->adapter_handle;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to destroy RNIC adapter err %d", err);
		return err;
	}

	return 0;
}

int mana_ib_gd_add_gid(const struct ib_gid_attr *attr, void **context)
{
	struct mana_ib_dev *mdev = container_of(attr->device, struct mana_ib_dev, ib_dev);
	enum rdma_network_type ntype = rdma_gid_attr_network_type(attr);
	struct mana_rnic_config_addr_resp resp = {};
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct mana_rnic_config_addr_req req = {};
	int err;

	if (ntype != RDMA_NETWORK_IPV4 && ntype != RDMA_NETWORK_IPV6) {
		ibdev_dbg(&mdev->ib_dev, "Unsupported rdma network type %d", ntype);
		return -EINVAL;
	}

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_CONFIG_IP_ADDR, sizeof(req), sizeof(resp));
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.adapter = mdev->adapter_handle;
	req.op = ADDR_OP_ADD;
	req.sgid_type = (ntype == RDMA_NETWORK_IPV6) ? SGID_TYPE_IPV6 : SGID_TYPE_IPV4;
	copy_in_reverse(req.ip_addr, attr->gid.raw, sizeof(union ib_gid));

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to config IP addr err %d\n", err);
		return err;
	}

	return 0;
}

int mana_ib_gd_del_gid(const struct ib_gid_attr *attr, void **context)
{
	struct mana_ib_dev *mdev = container_of(attr->device, struct mana_ib_dev, ib_dev);
	enum rdma_network_type ntype = rdma_gid_attr_network_type(attr);
	struct mana_rnic_config_addr_resp resp = {};
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct mana_rnic_config_addr_req req = {};
	int err;

	if (ntype != RDMA_NETWORK_IPV4 && ntype != RDMA_NETWORK_IPV6) {
		ibdev_dbg(&mdev->ib_dev, "Unsupported rdma network type %d", ntype);
		return -EINVAL;
	}

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_CONFIG_IP_ADDR, sizeof(req), sizeof(resp));
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.adapter = mdev->adapter_handle;
	req.op = ADDR_OP_REMOVE;
	req.sgid_type = (ntype == RDMA_NETWORK_IPV6) ? SGID_TYPE_IPV6 : SGID_TYPE_IPV4;
	copy_in_reverse(req.ip_addr, attr->gid.raw, sizeof(union ib_gid));

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to config IP addr err %d\n", err);
		return err;
	}

	return 0;
}

int mana_ib_gd_config_mac(struct mana_ib_dev *mdev, enum mana_ib_addr_op op, u8 *mac)
{
	struct mana_rnic_config_mac_addr_resp resp = {};
	struct mana_rnic_config_mac_addr_req req = {};
	struct gdma_context *gc = mdev_to_gc(mdev);
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_CONFIG_MAC_ADDR, sizeof(req), sizeof(resp));
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.adapter = mdev->adapter_handle;
	req.op = op;
	copy_in_reverse(req.mac_addr, mac, ETH_ALEN);

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to config Mac addr err %d", err);
		return err;
	}

	return 0;
}

int mana_ib_gd_create_cq(struct mana_ib_dev *mdev, struct mana_ib_cq *cq, u32 doorbell)
{
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct mana_rnic_create_cq_resp resp = {};
	struct mana_rnic_create_cq_req req = {};
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_CREATE_CQ, sizeof(req), sizeof(resp));
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.adapter = mdev->adapter_handle;
	req.gdma_region = cq->queue.gdma_region;
	req.eq_id = mdev->eqs[cq->comp_vector]->id;
	req.doorbell_page = doorbell;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);

	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to create cq err %d", err);
		return err;
	}

	cq->queue.id  = resp.cq_id;
	cq->cq_handle = resp.cq_handle;
	/* The GDMA region is now owned by the CQ handle */
	cq->queue.gdma_region = GDMA_INVALID_DMA_REGION;

	return 0;
}

int mana_ib_gd_destroy_cq(struct mana_ib_dev *mdev, struct mana_ib_cq *cq)
{
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct mana_rnic_destroy_cq_resp resp = {};
	struct mana_rnic_destroy_cq_req req = {};
	int err;

	if (cq->cq_handle == INVALID_MANA_HANDLE)
		return 0;

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_DESTROY_CQ, sizeof(req), sizeof(resp));
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.adapter = mdev->adapter_handle;
	req.cq_handle = cq->cq_handle;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);

	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to destroy cq err %d", err);
		return err;
	}

	return 0;
}

int mana_ib_gd_create_rc_qp(struct mana_ib_dev *mdev, struct mana_ib_qp *qp,
			    struct ib_qp_init_attr *attr, u32 doorbell, u64 flags)
{
	struct mana_ib_cq *send_cq = container_of(qp->ibqp.send_cq, struct mana_ib_cq, ibcq);
	struct mana_ib_cq *recv_cq = container_of(qp->ibqp.recv_cq, struct mana_ib_cq, ibcq);
	struct mana_ib_pd *pd = container_of(qp->ibqp.pd, struct mana_ib_pd, ibpd);
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct mana_rnic_create_qp_resp resp = {};
	struct mana_rnic_create_qp_req req = {};
	int err, i;

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_CREATE_RC_QP, sizeof(req), sizeof(resp));
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.adapter = mdev->adapter_handle;
	req.pd_handle = pd->pd_handle;
	req.send_cq_handle = send_cq->cq_handle;
	req.recv_cq_handle = recv_cq->cq_handle;
	for (i = 0; i < MANA_RC_QUEUE_TYPE_MAX; i++)
		req.dma_region[i] = qp->rc_qp.queues[i].gdma_region;
	req.doorbell_page = doorbell;
	req.max_send_wr = attr->cap.max_send_wr;
	req.max_recv_wr = attr->cap.max_recv_wr;
	req.max_send_sge = attr->cap.max_send_sge;
	req.max_recv_sge = attr->cap.max_recv_sge;
	req.flags = flags;

	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to create rc qp err %d", err);
		return err;
	}
	qp->qp_handle = resp.rc_qp_handle;
	for (i = 0; i < MANA_RC_QUEUE_TYPE_MAX; i++) {
		qp->rc_qp.queues[i].id = resp.queue_ids[i];
		/* The GDMA regions are now owned by the RNIC QP handle */
		qp->rc_qp.queues[i].gdma_region = GDMA_INVALID_DMA_REGION;
	}
	return 0;
}

int mana_ib_gd_destroy_rc_qp(struct mana_ib_dev *mdev, struct mana_ib_qp *qp)
{
	struct mana_rnic_destroy_rc_qp_resp resp = {0};
	struct mana_rnic_destroy_rc_qp_req req = {0};
	struct gdma_context *gc = mdev_to_gc(mdev);
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_DESTROY_RC_QP, sizeof(req), sizeof(resp));
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.adapter = mdev->adapter_handle;
	req.rc_qp_handle = qp->qp_handle;
	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to destroy rc qp err %d", err);
		return err;
	}
	return 0;
}

int mana_ib_gd_create_ud_qp(struct mana_ib_dev *mdev, struct mana_ib_qp *qp,
			    struct ib_qp_init_attr *attr, u32 doorbell, u32 type)
{
	struct mana_ib_cq *send_cq = container_of(qp->ibqp.send_cq, struct mana_ib_cq, ibcq);
	struct mana_ib_cq *recv_cq = container_of(qp->ibqp.recv_cq, struct mana_ib_cq, ibcq);
	struct mana_ib_pd *pd = container_of(qp->ibqp.pd, struct mana_ib_pd, ibpd);
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct mana_rnic_create_udqp_resp resp = {};
	struct mana_rnic_create_udqp_req req = {};
	int err, i;

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_CREATE_UD_QP, sizeof(req), sizeof(resp));
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.adapter = mdev->adapter_handle;
	req.pd_handle = pd->pd_handle;
	req.send_cq_handle = send_cq->cq_handle;
	req.recv_cq_handle = recv_cq->cq_handle;
	for (i = 0; i < MANA_UD_QUEUE_TYPE_MAX; i++)
		req.dma_region[i] = qp->ud_qp.queues[i].gdma_region;
	req.doorbell_page = doorbell;
	req.max_send_wr = attr->cap.max_send_wr;
	req.max_recv_wr = attr->cap.max_recv_wr;
	req.max_send_sge = attr->cap.max_send_sge;
	req.max_recv_sge = attr->cap.max_recv_sge;
	req.qp_type = type;
	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to create ud qp err %d", err);
		return err;
	}
	qp->qp_handle = resp.qp_handle;
	for (i = 0; i < MANA_UD_QUEUE_TYPE_MAX; i++) {
		qp->ud_qp.queues[i].id = resp.queue_ids[i];
		/* The GDMA regions are now owned by the RNIC QP handle */
		qp->ud_qp.queues[i].gdma_region = GDMA_INVALID_DMA_REGION;
	}
	return 0;
}

int mana_ib_gd_destroy_ud_qp(struct mana_ib_dev *mdev, struct mana_ib_qp *qp)
{
	struct mana_rnic_destroy_udqp_resp resp = {0};
	struct mana_rnic_destroy_udqp_req req = {0};
	struct gdma_context *gc = mdev_to_gc(mdev);
	int err;

	mana_gd_init_req_hdr(&req.hdr, MANA_IB_DESTROY_UD_QP, sizeof(req), sizeof(resp));
	req.hdr.dev_id = gc->mana_ib.dev_id;
	req.adapter = mdev->adapter_handle;
	req.qp_handle = qp->qp_handle;
	err = mana_gd_send_request(gc, sizeof(req), &req, sizeof(resp), &resp);
	if (err) {
		ibdev_err(&mdev->ib_dev, "Failed to destroy ud qp err %d", err);
		return err;
	}
	return 0;
}
