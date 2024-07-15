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
	req.alignment = 1;

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
	int err;

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
	create_req->gdma_page_type = order_base_2(page_sz) - PAGE_SHIFT;
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

	ret = rdma_user_mmap_io(ibcontext, vma, pfn, gc->db_page_size, prot,
				NULL);
	if (ret)
		ibdev_dbg(ibdev, "can't rdma_user_mmap_io ret %d\n", ret);
	else
		ibdev_dbg(ibdev, "mapped I/O pfn 0x%llx page_size %u, ret %d\n",
			  pfn, gc->db_page_size, ret);

	return ret;
}

int mana_ib_get_port_immutable(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_immutable *immutable)
{
	/*
	 * This version only support RAW_PACKET
	 * other values need to be filled for other types
	 */
	immutable->core_cap_flags = RDMA_CORE_PORT_RAW_PACKET;

	return 0;
}

int mana_ib_query_device(struct ib_device *ibdev, struct ib_device_attr *props,
			 struct ib_udata *uhw)
{
	struct mana_ib_dev *dev = container_of(ibdev,
			struct mana_ib_dev, ib_dev);

	props->max_qp = dev->adapter_caps.max_qp_count;
	props->max_qp_wr = dev->adapter_caps.max_qp_wr;
	props->max_cq = dev->adapter_caps.max_cq_count;
	props->max_cqe = dev->adapter_caps.max_qp_wr;
	props->max_mr = dev->adapter_caps.max_mr_count;
	props->max_mr_size = MANA_IB_MAX_MR_SIZE;
	props->max_send_sge = dev->adapter_caps.max_send_sge_count;
	props->max_recv_sge = dev->adapter_caps.max_recv_sge_count;

	return 0;
}

int mana_ib_query_port(struct ib_device *ibdev, u32 port,
		       struct ib_port_attr *props)
{
	/* This version doesn't return port properties */
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
	req.hdr.resp.msg_version = GDMA_MESSAGE_V3;
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

	return 0;
}
