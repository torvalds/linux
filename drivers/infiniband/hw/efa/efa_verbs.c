// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2018-2022 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/vmalloc.h>
#include <linux/log2.h>

#include <rdma/ib_addr.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#include <rdma/uverbs_ioctl.h>

#include "efa.h"
#include "efa_io_defs.h"

enum {
	EFA_MMAP_DMA_PAGE = 0,
	EFA_MMAP_IO_WC,
	EFA_MMAP_IO_NC,
};

#define EFA_AENQ_ENABLED_GROUPS \
	(BIT(EFA_ADMIN_FATAL_ERROR) | BIT(EFA_ADMIN_WARNING) | \
	 BIT(EFA_ADMIN_NOTIFICATION) | BIT(EFA_ADMIN_KEEP_ALIVE))

struct efa_user_mmap_entry {
	struct rdma_user_mmap_entry rdma_entry;
	u64 address;
	u8 mmap_flag;
};

#define EFA_DEFINE_DEVICE_STATS(op) \
	op(EFA_SUBMITTED_CMDS, "submitted_cmds") \
	op(EFA_COMPLETED_CMDS, "completed_cmds") \
	op(EFA_CMDS_ERR, "cmds_err") \
	op(EFA_NO_COMPLETION_CMDS, "no_completion_cmds") \
	op(EFA_KEEP_ALIVE_RCVD, "keep_alive_rcvd") \
	op(EFA_ALLOC_PD_ERR, "alloc_pd_err") \
	op(EFA_CREATE_QP_ERR, "create_qp_err") \
	op(EFA_CREATE_CQ_ERR, "create_cq_err") \
	op(EFA_REG_MR_ERR, "reg_mr_err") \
	op(EFA_ALLOC_UCONTEXT_ERR, "alloc_ucontext_err") \
	op(EFA_CREATE_AH_ERR, "create_ah_err") \
	op(EFA_MMAP_ERR, "mmap_err")

#define EFA_DEFINE_PORT_STATS(op) \
	op(EFA_TX_BYTES, "tx_bytes") \
	op(EFA_TX_PKTS, "tx_pkts") \
	op(EFA_RX_BYTES, "rx_bytes") \
	op(EFA_RX_PKTS, "rx_pkts") \
	op(EFA_RX_DROPS, "rx_drops") \
	op(EFA_SEND_BYTES, "send_bytes") \
	op(EFA_SEND_WRS, "send_wrs") \
	op(EFA_RECV_BYTES, "recv_bytes") \
	op(EFA_RECV_WRS, "recv_wrs") \
	op(EFA_RDMA_READ_WRS, "rdma_read_wrs") \
	op(EFA_RDMA_READ_BYTES, "rdma_read_bytes") \
	op(EFA_RDMA_READ_WR_ERR, "rdma_read_wr_err") \
	op(EFA_RDMA_READ_RESP_BYTES, "rdma_read_resp_bytes") \

#define EFA_STATS_ENUM(ename, name) ename,
#define EFA_STATS_STR(ename, nam) \
	[ename].name = nam,

enum efa_hw_device_stats {
	EFA_DEFINE_DEVICE_STATS(EFA_STATS_ENUM)
};

static const struct rdma_stat_desc efa_device_stats_descs[] = {
	EFA_DEFINE_DEVICE_STATS(EFA_STATS_STR)
};

enum efa_hw_port_stats {
	EFA_DEFINE_PORT_STATS(EFA_STATS_ENUM)
};

static const struct rdma_stat_desc efa_port_stats_descs[] = {
	EFA_DEFINE_PORT_STATS(EFA_STATS_STR)
};

#define EFA_CHUNK_PAYLOAD_SHIFT       12
#define EFA_CHUNK_PAYLOAD_SIZE        BIT(EFA_CHUNK_PAYLOAD_SHIFT)
#define EFA_CHUNK_PAYLOAD_PTR_SIZE    8

#define EFA_CHUNK_SHIFT               12
#define EFA_CHUNK_SIZE                BIT(EFA_CHUNK_SHIFT)
#define EFA_CHUNK_PTR_SIZE            sizeof(struct efa_com_ctrl_buff_info)

#define EFA_PTRS_PER_CHUNK \
	((EFA_CHUNK_SIZE - EFA_CHUNK_PTR_SIZE) / EFA_CHUNK_PAYLOAD_PTR_SIZE)

#define EFA_CHUNK_USED_SIZE \
	((EFA_PTRS_PER_CHUNK * EFA_CHUNK_PAYLOAD_PTR_SIZE) + EFA_CHUNK_PTR_SIZE)

struct pbl_chunk {
	dma_addr_t dma_addr;
	u64 *buf;
	u32 length;
};

struct pbl_chunk_list {
	struct pbl_chunk *chunks;
	unsigned int size;
};

struct pbl_context {
	union {
		struct {
			dma_addr_t dma_addr;
		} continuous;
		struct {
			u32 pbl_buf_size_in_pages;
			struct scatterlist *sgl;
			int sg_dma_cnt;
			struct pbl_chunk_list chunk_list;
		} indirect;
	} phys;
	u64 *pbl_buf;
	u32 pbl_buf_size_in_bytes;
	u8 physically_continuous;
};

static inline struct efa_dev *to_edev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct efa_dev, ibdev);
}

static inline struct efa_ucontext *to_eucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct efa_ucontext, ibucontext);
}

static inline struct efa_pd *to_epd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct efa_pd, ibpd);
}

static inline struct efa_mr *to_emr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct efa_mr, ibmr);
}

static inline struct efa_qp *to_eqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct efa_qp, ibqp);
}

static inline struct efa_cq *to_ecq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct efa_cq, ibcq);
}

static inline struct efa_ah *to_eah(struct ib_ah *ibah)
{
	return container_of(ibah, struct efa_ah, ibah);
}

static inline struct efa_user_mmap_entry *
to_emmap(struct rdma_user_mmap_entry *rdma_entry)
{
	return container_of(rdma_entry, struct efa_user_mmap_entry, rdma_entry);
}

#define EFA_DEV_CAP(dev, cap) \
	((dev)->dev_attr.device_caps & \
	 EFA_ADMIN_FEATURE_DEVICE_ATTR_DESC_##cap##_MASK)

#define is_reserved_cleared(reserved) \
	!memchr_inv(reserved, 0, sizeof(reserved))

static void *efa_zalloc_mapped(struct efa_dev *dev, dma_addr_t *dma_addr,
			       size_t size, enum dma_data_direction dir)
{
	void *addr;

	addr = alloc_pages_exact(size, GFP_KERNEL | __GFP_ZERO);
	if (!addr)
		return NULL;

	*dma_addr = dma_map_single(&dev->pdev->dev, addr, size, dir);
	if (dma_mapping_error(&dev->pdev->dev, *dma_addr)) {
		ibdev_err(&dev->ibdev, "Failed to map DMA address\n");
		free_pages_exact(addr, size);
		return NULL;
	}

	return addr;
}

static void efa_free_mapped(struct efa_dev *dev, void *cpu_addr,
			    dma_addr_t dma_addr,
			    size_t size, enum dma_data_direction dir)
{
	dma_unmap_single(&dev->pdev->dev, dma_addr, size, dir);
	free_pages_exact(cpu_addr, size);
}

int efa_query_device(struct ib_device *ibdev,
		     struct ib_device_attr *props,
		     struct ib_udata *udata)
{
	struct efa_com_get_device_attr_result *dev_attr;
	struct efa_ibv_ex_query_device_resp resp = {};
	struct efa_dev *dev = to_edev(ibdev);
	int err;

	if (udata && udata->inlen &&
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
		ibdev_dbg(ibdev,
			  "Incompatible ABI params, udata not cleared\n");
		return -EINVAL;
	}

	dev_attr = &dev->dev_attr;

	memset(props, 0, sizeof(*props));
	props->max_mr_size = dev_attr->max_mr_pages * PAGE_SIZE;
	props->page_size_cap = dev_attr->page_size_cap;
	props->vendor_id = dev->pdev->vendor;
	props->vendor_part_id = dev->pdev->device;
	props->hw_ver = dev->pdev->subsystem_device;
	props->max_qp = dev_attr->max_qp;
	props->max_cq = dev_attr->max_cq;
	props->max_pd = dev_attr->max_pd;
	props->max_mr = dev_attr->max_mr;
	props->max_ah = dev_attr->max_ah;
	props->max_cqe = dev_attr->max_cq_depth;
	props->max_qp_wr = min_t(u32, dev_attr->max_sq_depth,
				 dev_attr->max_rq_depth);
	props->max_send_sge = dev_attr->max_sq_sge;
	props->max_recv_sge = dev_attr->max_rq_sge;
	props->max_sge_rd = dev_attr->max_wr_rdma_sge;
	props->max_pkeys = 1;

	if (udata && udata->outlen) {
		resp.max_sq_sge = dev_attr->max_sq_sge;
		resp.max_rq_sge = dev_attr->max_rq_sge;
		resp.max_sq_wr = dev_attr->max_sq_depth;
		resp.max_rq_wr = dev_attr->max_rq_depth;
		resp.max_rdma_size = dev_attr->max_rdma_size;

		resp.device_caps |= EFA_QUERY_DEVICE_CAPS_CQ_WITH_SGID;
		if (EFA_DEV_CAP(dev, RDMA_READ))
			resp.device_caps |= EFA_QUERY_DEVICE_CAPS_RDMA_READ;

		if (EFA_DEV_CAP(dev, RNR_RETRY))
			resp.device_caps |= EFA_QUERY_DEVICE_CAPS_RNR_RETRY;

		if (dev->neqs)
			resp.device_caps |= EFA_QUERY_DEVICE_CAPS_CQ_NOTIFICATIONS;

		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(ibdev,
				  "Failed to copy udata for query_device\n");
			return err;
		}
	}

	return 0;
}

int efa_query_port(struct ib_device *ibdev, u32 port,
		   struct ib_port_attr *props)
{
	struct efa_dev *dev = to_edev(ibdev);

	props->lmc = 1;

	props->state = IB_PORT_ACTIVE;
	props->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	props->gid_tbl_len = 1;
	props->pkey_tbl_len = 1;
	props->active_speed = IB_SPEED_EDR;
	props->active_width = IB_WIDTH_4X;
	props->max_mtu = ib_mtu_int_to_enum(dev->dev_attr.mtu);
	props->active_mtu = ib_mtu_int_to_enum(dev->dev_attr.mtu);
	props->max_msg_sz = dev->dev_attr.mtu;
	props->max_vl_num = 1;

	return 0;
}

int efa_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		 int qp_attr_mask,
		 struct ib_qp_init_attr *qp_init_attr)
{
	struct efa_dev *dev = to_edev(ibqp->device);
	struct efa_com_query_qp_params params = {};
	struct efa_com_query_qp_result result;
	struct efa_qp *qp = to_eqp(ibqp);
	int err;

#define EFA_QUERY_QP_SUPP_MASK \
	(IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_PORT | \
	 IB_QP_QKEY | IB_QP_SQ_PSN | IB_QP_CAP | IB_QP_RNR_RETRY)

	if (qp_attr_mask & ~EFA_QUERY_QP_SUPP_MASK) {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported qp_attr_mask[%#x] supported[%#x]\n",
			  qp_attr_mask, EFA_QUERY_QP_SUPP_MASK);
		return -EOPNOTSUPP;
	}

	memset(qp_attr, 0, sizeof(*qp_attr));
	memset(qp_init_attr, 0, sizeof(*qp_init_attr));

	params.qp_handle = qp->qp_handle;
	err = efa_com_query_qp(&dev->edev, &params, &result);
	if (err)
		return err;

	qp_attr->qp_state = result.qp_state;
	qp_attr->qkey = result.qkey;
	qp_attr->sq_psn = result.sq_psn;
	qp_attr->sq_draining = result.sq_draining;
	qp_attr->port_num = 1;
	qp_attr->rnr_retry = result.rnr_retry;

	qp_attr->cap.max_send_wr = qp->max_send_wr;
	qp_attr->cap.max_recv_wr = qp->max_recv_wr;
	qp_attr->cap.max_send_sge = qp->max_send_sge;
	qp_attr->cap.max_recv_sge = qp->max_recv_sge;
	qp_attr->cap.max_inline_data = qp->max_inline_data;

	qp_init_attr->qp_type = ibqp->qp_type;
	qp_init_attr->recv_cq = ibqp->recv_cq;
	qp_init_attr->send_cq = ibqp->send_cq;
	qp_init_attr->qp_context = ibqp->qp_context;
	qp_init_attr->cap = qp_attr->cap;

	return 0;
}

int efa_query_gid(struct ib_device *ibdev, u32 port, int index,
		  union ib_gid *gid)
{
	struct efa_dev *dev = to_edev(ibdev);

	memcpy(gid->raw, dev->dev_attr.addr, sizeof(dev->dev_attr.addr));

	return 0;
}

int efa_query_pkey(struct ib_device *ibdev, u32 port, u16 index,
		   u16 *pkey)
{
	if (index > 0)
		return -EINVAL;

	*pkey = 0xffff;
	return 0;
}

static int efa_pd_dealloc(struct efa_dev *dev, u16 pdn)
{
	struct efa_com_dealloc_pd_params params = {
		.pdn = pdn,
	};

	return efa_com_dealloc_pd(&dev->edev, &params);
}

int efa_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_ibv_alloc_pd_resp resp = {};
	struct efa_com_alloc_pd_result result;
	struct efa_pd *pd = to_epd(ibpd);
	int err;

	if (udata->inlen &&
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, udata not cleared\n");
		err = -EINVAL;
		goto err_out;
	}

	err = efa_com_alloc_pd(&dev->edev, &result);
	if (err)
		goto err_out;

	pd->pdn = result.pdn;
	resp.pdn = result.pdn;

	if (udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(&dev->ibdev,
				  "Failed to copy udata for alloc_pd\n");
			goto err_dealloc_pd;
		}
	}

	ibdev_dbg(&dev->ibdev, "Allocated pd[%d]\n", pd->pdn);

	return 0;

err_dealloc_pd:
	efa_pd_dealloc(dev, result.pdn);
err_out:
	atomic64_inc(&dev->stats.alloc_pd_err);
	return err;
}

int efa_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_pd *pd = to_epd(ibpd);

	ibdev_dbg(&dev->ibdev, "Dealloc pd[%d]\n", pd->pdn);
	efa_pd_dealloc(dev, pd->pdn);
	return 0;
}

static int efa_destroy_qp_handle(struct efa_dev *dev, u32 qp_handle)
{
	struct efa_com_destroy_qp_params params = { .qp_handle = qp_handle };

	return efa_com_destroy_qp(&dev->edev, &params);
}

static void efa_qp_user_mmap_entries_remove(struct efa_qp *qp)
{
	rdma_user_mmap_entry_remove(qp->rq_mmap_entry);
	rdma_user_mmap_entry_remove(qp->rq_db_mmap_entry);
	rdma_user_mmap_entry_remove(qp->llq_desc_mmap_entry);
	rdma_user_mmap_entry_remove(qp->sq_db_mmap_entry);
}

int efa_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibqp->pd->device);
	struct efa_qp *qp = to_eqp(ibqp);
	int err;

	ibdev_dbg(&dev->ibdev, "Destroy qp[%u]\n", ibqp->qp_num);

	efa_qp_user_mmap_entries_remove(qp);

	err = efa_destroy_qp_handle(dev, qp->qp_handle);
	if (err)
		return err;

	if (qp->rq_cpu_addr) {
		ibdev_dbg(&dev->ibdev,
			  "qp->cpu_addr[0x%p] freed: size[%lu], dma[%pad]\n",
			  qp->rq_cpu_addr, qp->rq_size,
			  &qp->rq_dma_addr);
		efa_free_mapped(dev, qp->rq_cpu_addr, qp->rq_dma_addr,
				qp->rq_size, DMA_TO_DEVICE);
	}

	return 0;
}

static struct rdma_user_mmap_entry*
efa_user_mmap_entry_insert(struct ib_ucontext *ucontext,
			   u64 address, size_t length,
			   u8 mmap_flag, u64 *offset)
{
	struct efa_user_mmap_entry *entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	int err;

	if (!entry)
		return NULL;

	entry->address = address;
	entry->mmap_flag = mmap_flag;

	err = rdma_user_mmap_entry_insert(ucontext, &entry->rdma_entry,
					  length);
	if (err) {
		kfree(entry);
		return NULL;
	}
	*offset = rdma_user_mmap_get_offset(&entry->rdma_entry);

	return &entry->rdma_entry;
}

static int qp_mmap_entries_setup(struct efa_qp *qp,
				 struct efa_dev *dev,
				 struct efa_ucontext *ucontext,
				 struct efa_com_create_qp_params *params,
				 struct efa_ibv_create_qp_resp *resp)
{
	size_t length;
	u64 address;

	address = dev->db_bar_addr + resp->sq_db_offset;
	qp->sq_db_mmap_entry =
		efa_user_mmap_entry_insert(&ucontext->ibucontext,
					   address,
					   PAGE_SIZE, EFA_MMAP_IO_NC,
					   &resp->sq_db_mmap_key);
	if (!qp->sq_db_mmap_entry)
		return -ENOMEM;

	resp->sq_db_offset &= ~PAGE_MASK;

	address = dev->mem_bar_addr + resp->llq_desc_offset;
	length = PAGE_ALIGN(params->sq_ring_size_in_bytes +
			    (resp->llq_desc_offset & ~PAGE_MASK));

	qp->llq_desc_mmap_entry =
		efa_user_mmap_entry_insert(&ucontext->ibucontext,
					   address, length,
					   EFA_MMAP_IO_WC,
					   &resp->llq_desc_mmap_key);
	if (!qp->llq_desc_mmap_entry)
		goto err_remove_mmap;

	resp->llq_desc_offset &= ~PAGE_MASK;

	if (qp->rq_size) {
		address = dev->db_bar_addr + resp->rq_db_offset;

		qp->rq_db_mmap_entry =
			efa_user_mmap_entry_insert(&ucontext->ibucontext,
						   address, PAGE_SIZE,
						   EFA_MMAP_IO_NC,
						   &resp->rq_db_mmap_key);
		if (!qp->rq_db_mmap_entry)
			goto err_remove_mmap;

		resp->rq_db_offset &= ~PAGE_MASK;

		address = virt_to_phys(qp->rq_cpu_addr);
		qp->rq_mmap_entry =
			efa_user_mmap_entry_insert(&ucontext->ibucontext,
						   address, qp->rq_size,
						   EFA_MMAP_DMA_PAGE,
						   &resp->rq_mmap_key);
		if (!qp->rq_mmap_entry)
			goto err_remove_mmap;

		resp->rq_mmap_size = qp->rq_size;
	}

	return 0;

err_remove_mmap:
	efa_qp_user_mmap_entries_remove(qp);

	return -ENOMEM;
}

static int efa_qp_validate_cap(struct efa_dev *dev,
			       struct ib_qp_init_attr *init_attr)
{
	if (init_attr->cap.max_send_wr > dev->dev_attr.max_sq_depth) {
		ibdev_dbg(&dev->ibdev,
			  "qp: requested send wr[%u] exceeds the max[%u]\n",
			  init_attr->cap.max_send_wr,
			  dev->dev_attr.max_sq_depth);
		return -EINVAL;
	}
	if (init_attr->cap.max_recv_wr > dev->dev_attr.max_rq_depth) {
		ibdev_dbg(&dev->ibdev,
			  "qp: requested receive wr[%u] exceeds the max[%u]\n",
			  init_attr->cap.max_recv_wr,
			  dev->dev_attr.max_rq_depth);
		return -EINVAL;
	}
	if (init_attr->cap.max_send_sge > dev->dev_attr.max_sq_sge) {
		ibdev_dbg(&dev->ibdev,
			  "qp: requested sge send[%u] exceeds the max[%u]\n",
			  init_attr->cap.max_send_sge, dev->dev_attr.max_sq_sge);
		return -EINVAL;
	}
	if (init_attr->cap.max_recv_sge > dev->dev_attr.max_rq_sge) {
		ibdev_dbg(&dev->ibdev,
			  "qp: requested sge recv[%u] exceeds the max[%u]\n",
			  init_attr->cap.max_recv_sge, dev->dev_attr.max_rq_sge);
		return -EINVAL;
	}
	if (init_attr->cap.max_inline_data > dev->dev_attr.inline_buf_size) {
		ibdev_dbg(&dev->ibdev,
			  "qp: requested inline data[%u] exceeds the max[%u]\n",
			  init_attr->cap.max_inline_data,
			  dev->dev_attr.inline_buf_size);
		return -EINVAL;
	}

	return 0;
}

static int efa_qp_validate_attr(struct efa_dev *dev,
				struct ib_qp_init_attr *init_attr)
{
	if (init_attr->qp_type != IB_QPT_DRIVER &&
	    init_attr->qp_type != IB_QPT_UD) {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported qp type %d\n", init_attr->qp_type);
		return -EOPNOTSUPP;
	}

	if (init_attr->srq) {
		ibdev_dbg(&dev->ibdev, "SRQ is not supported\n");
		return -EOPNOTSUPP;
	}

	if (init_attr->create_flags) {
		ibdev_dbg(&dev->ibdev, "Unsupported create flags\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

int efa_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *init_attr,
		  struct ib_udata *udata)
{
	struct efa_com_create_qp_params create_qp_params = {};
	struct efa_com_create_qp_result create_qp_resp;
	struct efa_dev *dev = to_edev(ibqp->device);
	struct efa_ibv_create_qp_resp resp = {};
	struct efa_ibv_create_qp cmd = {};
	struct efa_qp *qp = to_eqp(ibqp);
	struct efa_ucontext *ucontext;
	int err;

	ucontext = rdma_udata_to_drv_context(udata, struct efa_ucontext,
					     ibucontext);

	err = efa_qp_validate_cap(dev, init_attr);
	if (err)
		goto err_out;

	err = efa_qp_validate_attr(dev, init_attr);
	if (err)
		goto err_out;

	if (offsetofend(typeof(cmd), driver_qp_type) > udata->inlen) {
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, no input udata\n");
		err = -EINVAL;
		goto err_out;
	}

	if (udata->inlen > sizeof(cmd) &&
	    !ib_is_udata_cleared(udata, sizeof(cmd),
				 udata->inlen - sizeof(cmd))) {
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, unknown fields in udata\n");
		err = -EINVAL;
		goto err_out;
	}

	err = ib_copy_from_udata(&cmd, udata,
				 min(sizeof(cmd), udata->inlen));
	if (err) {
		ibdev_dbg(&dev->ibdev,
			  "Cannot copy udata for create_qp\n");
		goto err_out;
	}

	if (cmd.comp_mask) {
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, unknown fields in udata\n");
		err = -EINVAL;
		goto err_out;
	}

	create_qp_params.uarn = ucontext->uarn;
	create_qp_params.pd = to_epd(ibqp->pd)->pdn;

	if (init_attr->qp_type == IB_QPT_UD) {
		create_qp_params.qp_type = EFA_ADMIN_QP_TYPE_UD;
	} else if (cmd.driver_qp_type == EFA_QP_DRIVER_TYPE_SRD) {
		create_qp_params.qp_type = EFA_ADMIN_QP_TYPE_SRD;
	} else {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported qp type %d driver qp type %d\n",
			  init_attr->qp_type, cmd.driver_qp_type);
		err = -EOPNOTSUPP;
		goto err_out;
	}

	ibdev_dbg(&dev->ibdev, "Create QP: qp type %d driver qp type %#x\n",
		  init_attr->qp_type, cmd.driver_qp_type);
	create_qp_params.send_cq_idx = to_ecq(init_attr->send_cq)->cq_idx;
	create_qp_params.recv_cq_idx = to_ecq(init_attr->recv_cq)->cq_idx;
	create_qp_params.sq_depth = init_attr->cap.max_send_wr;
	create_qp_params.sq_ring_size_in_bytes = cmd.sq_ring_size;

	create_qp_params.rq_depth = init_attr->cap.max_recv_wr;
	create_qp_params.rq_ring_size_in_bytes = cmd.rq_ring_size;
	qp->rq_size = PAGE_ALIGN(create_qp_params.rq_ring_size_in_bytes);
	if (qp->rq_size) {
		qp->rq_cpu_addr = efa_zalloc_mapped(dev, &qp->rq_dma_addr,
						    qp->rq_size, DMA_TO_DEVICE);
		if (!qp->rq_cpu_addr) {
			err = -ENOMEM;
			goto err_out;
		}

		ibdev_dbg(&dev->ibdev,
			  "qp->cpu_addr[0x%p] allocated: size[%lu], dma[%pad]\n",
			  qp->rq_cpu_addr, qp->rq_size, &qp->rq_dma_addr);
		create_qp_params.rq_base_addr = qp->rq_dma_addr;
	}

	err = efa_com_create_qp(&dev->edev, &create_qp_params,
				&create_qp_resp);
	if (err)
		goto err_free_mapped;

	resp.sq_db_offset = create_qp_resp.sq_db_offset;
	resp.rq_db_offset = create_qp_resp.rq_db_offset;
	resp.llq_desc_offset = create_qp_resp.llq_descriptors_offset;
	resp.send_sub_cq_idx = create_qp_resp.send_sub_cq_idx;
	resp.recv_sub_cq_idx = create_qp_resp.recv_sub_cq_idx;

	err = qp_mmap_entries_setup(qp, dev, ucontext, &create_qp_params,
				    &resp);
	if (err)
		goto err_destroy_qp;

	qp->qp_handle = create_qp_resp.qp_handle;
	qp->ibqp.qp_num = create_qp_resp.qp_num;
	qp->max_send_wr = init_attr->cap.max_send_wr;
	qp->max_recv_wr = init_attr->cap.max_recv_wr;
	qp->max_send_sge = init_attr->cap.max_send_sge;
	qp->max_recv_sge = init_attr->cap.max_recv_sge;
	qp->max_inline_data = init_attr->cap.max_inline_data;

	if (udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(&dev->ibdev,
				  "Failed to copy udata for qp[%u]\n",
				  create_qp_resp.qp_num);
			goto err_remove_mmap_entries;
		}
	}

	ibdev_dbg(&dev->ibdev, "Created qp[%d]\n", qp->ibqp.qp_num);

	return 0;

err_remove_mmap_entries:
	efa_qp_user_mmap_entries_remove(qp);
err_destroy_qp:
	efa_destroy_qp_handle(dev, create_qp_resp.qp_handle);
err_free_mapped:
	if (qp->rq_size)
		efa_free_mapped(dev, qp->rq_cpu_addr, qp->rq_dma_addr,
				qp->rq_size, DMA_TO_DEVICE);
err_out:
	atomic64_inc(&dev->stats.create_qp_err);
	return err;
}

static const struct {
	int			valid;
	enum ib_qp_attr_mask	req_param;
	enum ib_qp_attr_mask	opt_param;
} srd_qp_state_table[IB_QPS_ERR + 1][IB_QPS_ERR + 1] = {
	[IB_QPS_RESET] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_INIT]  = {
			.valid = 1,
			.req_param = IB_QP_PKEY_INDEX |
				     IB_QP_PORT |
				     IB_QP_QKEY,
		},
	},
	[IB_QPS_INIT] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR]   = { .valid = 1 },
		[IB_QPS_INIT]  = {
			.valid = 1,
			.opt_param = IB_QP_PKEY_INDEX |
				     IB_QP_PORT |
				     IB_QP_QKEY,
		},
		[IB_QPS_RTR]   = {
			.valid = 1,
			.opt_param = IB_QP_PKEY_INDEX |
				     IB_QP_QKEY,
		},
	},
	[IB_QPS_RTR] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR]   = { .valid = 1 },
		[IB_QPS_RTS]   = {
			.valid = 1,
			.req_param = IB_QP_SQ_PSN,
			.opt_param = IB_QP_CUR_STATE |
				     IB_QP_QKEY |
				     IB_QP_RNR_RETRY,

		}
	},
	[IB_QPS_RTS] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR]   = { .valid = 1 },
		[IB_QPS_RTS]   = {
			.valid = 1,
			.opt_param = IB_QP_CUR_STATE |
				     IB_QP_QKEY,
		},
		[IB_QPS_SQD] = {
			.valid = 1,
			.opt_param = IB_QP_EN_SQD_ASYNC_NOTIFY,
		},
	},
	[IB_QPS_SQD] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR]   = { .valid = 1 },
		[IB_QPS_RTS]   = {
			.valid = 1,
			.opt_param = IB_QP_CUR_STATE |
				     IB_QP_QKEY,
		},
		[IB_QPS_SQD] = {
			.valid = 1,
			.opt_param = IB_QP_PKEY_INDEX |
				     IB_QP_QKEY,
		}
	},
	[IB_QPS_SQE] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR]   = { .valid = 1 },
		[IB_QPS_RTS]   = {
			.valid = 1,
			.opt_param = IB_QP_CUR_STATE |
				     IB_QP_QKEY,
		}
	},
	[IB_QPS_ERR] = {
		[IB_QPS_RESET] = { .valid = 1 },
		[IB_QPS_ERR]   = { .valid = 1 },
	}
};

static bool efa_modify_srd_qp_is_ok(enum ib_qp_state cur_state,
				    enum ib_qp_state next_state,
				    enum ib_qp_attr_mask mask)
{
	enum ib_qp_attr_mask req_param, opt_param;

	if (mask & IB_QP_CUR_STATE  &&
	    cur_state != IB_QPS_RTR && cur_state != IB_QPS_RTS &&
	    cur_state != IB_QPS_SQD && cur_state != IB_QPS_SQE)
		return false;

	if (!srd_qp_state_table[cur_state][next_state].valid)
		return false;

	req_param = srd_qp_state_table[cur_state][next_state].req_param;
	opt_param = srd_qp_state_table[cur_state][next_state].opt_param;

	if ((mask & req_param) != req_param)
		return false;

	if (mask & ~(req_param | opt_param | IB_QP_STATE))
		return false;

	return true;
}

static int efa_modify_qp_validate(struct efa_dev *dev, struct efa_qp *qp,
				  struct ib_qp_attr *qp_attr, int qp_attr_mask,
				  enum ib_qp_state cur_state,
				  enum ib_qp_state new_state)
{
	int err;

#define EFA_MODIFY_QP_SUPP_MASK \
	(IB_QP_STATE | IB_QP_CUR_STATE | IB_QP_EN_SQD_ASYNC_NOTIFY | \
	 IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_QKEY | IB_QP_SQ_PSN | \
	 IB_QP_RNR_RETRY)

	if (qp_attr_mask & ~EFA_MODIFY_QP_SUPP_MASK) {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported qp_attr_mask[%#x] supported[%#x]\n",
			  qp_attr_mask, EFA_MODIFY_QP_SUPP_MASK);
		return -EOPNOTSUPP;
	}

	if (qp->ibqp.qp_type == IB_QPT_DRIVER)
		err = !efa_modify_srd_qp_is_ok(cur_state, new_state,
					       qp_attr_mask);
	else
		err = !ib_modify_qp_is_ok(cur_state, new_state, IB_QPT_UD,
					  qp_attr_mask);

	if (err) {
		ibdev_dbg(&dev->ibdev, "Invalid modify QP parameters\n");
		return -EINVAL;
	}

	if ((qp_attr_mask & IB_QP_PORT) && qp_attr->port_num != 1) {
		ibdev_dbg(&dev->ibdev, "Can't change port num\n");
		return -EOPNOTSUPP;
	}

	if ((qp_attr_mask & IB_QP_PKEY_INDEX) && qp_attr->pkey_index) {
		ibdev_dbg(&dev->ibdev, "Can't change pkey index\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

int efa_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		  int qp_attr_mask, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibqp->device);
	struct efa_com_modify_qp_params params = {};
	struct efa_qp *qp = to_eqp(ibqp);
	enum ib_qp_state cur_state;
	enum ib_qp_state new_state;
	int err;

	if (qp_attr_mask & ~IB_QP_ATTR_STANDARD_BITS)
		return -EOPNOTSUPP;

	if (udata->inlen &&
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, udata not cleared\n");
		return -EINVAL;
	}

	cur_state = qp_attr_mask & IB_QP_CUR_STATE ? qp_attr->cur_qp_state :
						     qp->state;
	new_state = qp_attr_mask & IB_QP_STATE ? qp_attr->qp_state : cur_state;

	err = efa_modify_qp_validate(dev, qp, qp_attr, qp_attr_mask, cur_state,
				     new_state);
	if (err)
		return err;

	params.qp_handle = qp->qp_handle;

	if (qp_attr_mask & IB_QP_STATE) {
		EFA_SET(&params.modify_mask, EFA_ADMIN_MODIFY_QP_CMD_QP_STATE,
			1);
		EFA_SET(&params.modify_mask,
			EFA_ADMIN_MODIFY_QP_CMD_CUR_QP_STATE, 1);
		params.cur_qp_state = cur_state;
		params.qp_state = new_state;
	}

	if (qp_attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY) {
		EFA_SET(&params.modify_mask,
			EFA_ADMIN_MODIFY_QP_CMD_SQ_DRAINED_ASYNC_NOTIFY, 1);
		params.sq_drained_async_notify = qp_attr->en_sqd_async_notify;
	}

	if (qp_attr_mask & IB_QP_QKEY) {
		EFA_SET(&params.modify_mask, EFA_ADMIN_MODIFY_QP_CMD_QKEY, 1);
		params.qkey = qp_attr->qkey;
	}

	if (qp_attr_mask & IB_QP_SQ_PSN) {
		EFA_SET(&params.modify_mask, EFA_ADMIN_MODIFY_QP_CMD_SQ_PSN, 1);
		params.sq_psn = qp_attr->sq_psn;
	}

	if (qp_attr_mask & IB_QP_RNR_RETRY) {
		EFA_SET(&params.modify_mask, EFA_ADMIN_MODIFY_QP_CMD_RNR_RETRY,
			1);
		params.rnr_retry = qp_attr->rnr_retry;
	}

	err = efa_com_modify_qp(&dev->edev, &params);
	if (err)
		return err;

	qp->state = new_state;

	return 0;
}

static int efa_destroy_cq_idx(struct efa_dev *dev, int cq_idx)
{
	struct efa_com_destroy_cq_params params = { .cq_idx = cq_idx };

	return efa_com_destroy_cq(&dev->edev, &params);
}

static void efa_cq_user_mmap_entries_remove(struct efa_cq *cq)
{
	rdma_user_mmap_entry_remove(cq->db_mmap_entry);
	rdma_user_mmap_entry_remove(cq->mmap_entry);
}

int efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibcq->device);
	struct efa_cq *cq = to_ecq(ibcq);

	ibdev_dbg(&dev->ibdev,
		  "Destroy cq[%d] virt[0x%p] freed: size[%lu], dma[%pad]\n",
		  cq->cq_idx, cq->cpu_addr, cq->size, &cq->dma_addr);

	efa_cq_user_mmap_entries_remove(cq);
	efa_destroy_cq_idx(dev, cq->cq_idx);
	if (cq->eq) {
		xa_erase(&dev->cqs_xa, cq->cq_idx);
		synchronize_irq(cq->eq->irq.irqn);
	}
	efa_free_mapped(dev, cq->cpu_addr, cq->dma_addr, cq->size,
			DMA_FROM_DEVICE);
	return 0;
}

static struct efa_eq *efa_vec2eq(struct efa_dev *dev, int vec)
{
	return &dev->eqs[vec];
}

static int cq_mmap_entries_setup(struct efa_dev *dev, struct efa_cq *cq,
				 struct efa_ibv_create_cq_resp *resp,
				 bool db_valid)
{
	resp->q_mmap_size = cq->size;
	cq->mmap_entry = efa_user_mmap_entry_insert(&cq->ucontext->ibucontext,
						    virt_to_phys(cq->cpu_addr),
						    cq->size, EFA_MMAP_DMA_PAGE,
						    &resp->q_mmap_key);
	if (!cq->mmap_entry)
		return -ENOMEM;

	if (db_valid) {
		cq->db_mmap_entry =
			efa_user_mmap_entry_insert(&cq->ucontext->ibucontext,
						   dev->db_bar_addr + resp->db_off,
						   PAGE_SIZE, EFA_MMAP_IO_NC,
						   &resp->db_mmap_key);
		if (!cq->db_mmap_entry) {
			rdma_user_mmap_entry_remove(cq->mmap_entry);
			return -ENOMEM;
		}

		resp->db_off &= ~PAGE_MASK;
		resp->comp_mask |= EFA_CREATE_CQ_RESP_DB_OFF;
	}

	return 0;
}

int efa_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		  struct ib_udata *udata)
{
	struct efa_ucontext *ucontext = rdma_udata_to_drv_context(
		udata, struct efa_ucontext, ibucontext);
	struct efa_com_create_cq_params params = {};
	struct efa_ibv_create_cq_resp resp = {};
	struct efa_com_create_cq_result result;
	struct ib_device *ibdev = ibcq->device;
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_ibv_create_cq cmd = {};
	struct efa_cq *cq = to_ecq(ibcq);
	int entries = attr->cqe;
	bool set_src_addr;
	int err;

	ibdev_dbg(ibdev, "create_cq entries %d\n", entries);

	if (attr->flags)
		return -EOPNOTSUPP;

	if (entries < 1 || entries > dev->dev_attr.max_cq_depth) {
		ibdev_dbg(ibdev,
			  "cq: requested entries[%u] non-positive or greater than max[%u]\n",
			  entries, dev->dev_attr.max_cq_depth);
		err = -EINVAL;
		goto err_out;
	}

	if (offsetofend(typeof(cmd), num_sub_cqs) > udata->inlen) {
		ibdev_dbg(ibdev,
			  "Incompatible ABI params, no input udata\n");
		err = -EINVAL;
		goto err_out;
	}

	if (udata->inlen > sizeof(cmd) &&
	    !ib_is_udata_cleared(udata, sizeof(cmd),
				 udata->inlen - sizeof(cmd))) {
		ibdev_dbg(ibdev,
			  "Incompatible ABI params, unknown fields in udata\n");
		err = -EINVAL;
		goto err_out;
	}

	err = ib_copy_from_udata(&cmd, udata,
				 min(sizeof(cmd), udata->inlen));
	if (err) {
		ibdev_dbg(ibdev, "Cannot copy udata for create_cq\n");
		goto err_out;
	}

	if (cmd.comp_mask || !is_reserved_cleared(cmd.reserved_58)) {
		ibdev_dbg(ibdev,
			  "Incompatible ABI params, unknown fields in udata\n");
		err = -EINVAL;
		goto err_out;
	}

	set_src_addr = !!(cmd.flags & EFA_CREATE_CQ_WITH_SGID);
	if ((cmd.cq_entry_size != sizeof(struct efa_io_rx_cdesc_ex)) &&
	    (set_src_addr ||
	     cmd.cq_entry_size != sizeof(struct efa_io_rx_cdesc))) {
		ibdev_dbg(ibdev,
			  "Invalid entry size [%u]\n", cmd.cq_entry_size);
		err = -EINVAL;
		goto err_out;
	}

	if (cmd.num_sub_cqs != dev->dev_attr.sub_cqs_per_cq) {
		ibdev_dbg(ibdev,
			  "Invalid number of sub cqs[%u] expected[%u]\n",
			  cmd.num_sub_cqs, dev->dev_attr.sub_cqs_per_cq);
		err = -EINVAL;
		goto err_out;
	}

	cq->ucontext = ucontext;
	cq->size = PAGE_ALIGN(cmd.cq_entry_size * entries * cmd.num_sub_cqs);
	cq->cpu_addr = efa_zalloc_mapped(dev, &cq->dma_addr, cq->size,
					 DMA_FROM_DEVICE);
	if (!cq->cpu_addr) {
		err = -ENOMEM;
		goto err_out;
	}

	params.uarn = cq->ucontext->uarn;
	params.cq_depth = entries;
	params.dma_addr = cq->dma_addr;
	params.entry_size_in_bytes = cmd.cq_entry_size;
	params.num_sub_cqs = cmd.num_sub_cqs;
	params.set_src_addr = set_src_addr;
	if (cmd.flags & EFA_CREATE_CQ_WITH_COMPLETION_CHANNEL) {
		cq->eq = efa_vec2eq(dev, attr->comp_vector);
		params.eqn = cq->eq->eeq.eqn;
		params.interrupt_mode_enabled = true;
	}

	err = efa_com_create_cq(&dev->edev, &params, &result);
	if (err)
		goto err_free_mapped;

	resp.db_off = result.db_off;
	resp.cq_idx = result.cq_idx;
	cq->cq_idx = result.cq_idx;
	cq->ibcq.cqe = result.actual_depth;
	WARN_ON_ONCE(entries != result.actual_depth);

	err = cq_mmap_entries_setup(dev, cq, &resp, result.db_valid);
	if (err) {
		ibdev_dbg(ibdev, "Could not setup cq[%u] mmap entries\n",
			  cq->cq_idx);
		goto err_destroy_cq;
	}

	if (cq->eq) {
		err = xa_err(xa_store(&dev->cqs_xa, cq->cq_idx, cq, GFP_KERNEL));
		if (err) {
			ibdev_dbg(ibdev, "Failed to store cq[%u] in xarray\n",
				  cq->cq_idx);
			goto err_remove_mmap;
		}
	}

	if (udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(ibdev,
				  "Failed to copy udata for create_cq\n");
			goto err_xa_erase;
		}
	}

	ibdev_dbg(ibdev, "Created cq[%d], cq depth[%u]. dma[%pad] virt[0x%p]\n",
		  cq->cq_idx, result.actual_depth, &cq->dma_addr, cq->cpu_addr);

	return 0;

err_xa_erase:
	if (cq->eq)
		xa_erase(&dev->cqs_xa, cq->cq_idx);
err_remove_mmap:
	efa_cq_user_mmap_entries_remove(cq);
err_destroy_cq:
	efa_destroy_cq_idx(dev, cq->cq_idx);
err_free_mapped:
	efa_free_mapped(dev, cq->cpu_addr, cq->dma_addr, cq->size,
			DMA_FROM_DEVICE);

err_out:
	atomic64_inc(&dev->stats.create_cq_err);
	return err;
}

static int umem_to_page_list(struct efa_dev *dev,
			     struct ib_umem *umem,
			     u64 *page_list,
			     u32 hp_cnt,
			     u8 hp_shift)
{
	u32 pages_in_hp = BIT(hp_shift - PAGE_SHIFT);
	struct ib_block_iter biter;
	unsigned int hp_idx = 0;

	ibdev_dbg(&dev->ibdev, "hp_cnt[%u], pages_in_hp[%u]\n",
		  hp_cnt, pages_in_hp);

	rdma_umem_for_each_dma_block(umem, &biter, BIT(hp_shift))
		page_list[hp_idx++] = rdma_block_iter_dma_address(&biter);

	return 0;
}

static struct scatterlist *efa_vmalloc_buf_to_sg(u64 *buf, int page_cnt)
{
	struct scatterlist *sglist;
	struct page *pg;
	int i;

	sglist = kmalloc_array(page_cnt, sizeof(*sglist), GFP_KERNEL);
	if (!sglist)
		return NULL;
	sg_init_table(sglist, page_cnt);
	for (i = 0; i < page_cnt; i++) {
		pg = vmalloc_to_page(buf);
		if (!pg)
			goto err;
		sg_set_page(&sglist[i], pg, PAGE_SIZE, 0);
		buf += PAGE_SIZE / sizeof(*buf);
	}
	return sglist;

err:
	kfree(sglist);
	return NULL;
}

/*
 * create a chunk list of physical pages dma addresses from the supplied
 * scatter gather list
 */
static int pbl_chunk_list_create(struct efa_dev *dev, struct pbl_context *pbl)
{
	struct pbl_chunk_list *chunk_list = &pbl->phys.indirect.chunk_list;
	int page_cnt = pbl->phys.indirect.pbl_buf_size_in_pages;
	struct scatterlist *pages_sgl = pbl->phys.indirect.sgl;
	unsigned int chunk_list_size, chunk_idx, payload_idx;
	int sg_dma_cnt = pbl->phys.indirect.sg_dma_cnt;
	struct efa_com_ctrl_buff_info *ctrl_buf;
	u64 *cur_chunk_buf, *prev_chunk_buf;
	struct ib_block_iter biter;
	dma_addr_t dma_addr;
	int i;

	/* allocate a chunk list that consists of 4KB chunks */
	chunk_list_size = DIV_ROUND_UP(page_cnt, EFA_PTRS_PER_CHUNK);

	chunk_list->size = chunk_list_size;
	chunk_list->chunks = kcalloc(chunk_list_size,
				     sizeof(*chunk_list->chunks),
				     GFP_KERNEL);
	if (!chunk_list->chunks)
		return -ENOMEM;

	ibdev_dbg(&dev->ibdev,
		  "chunk_list_size[%u] - pages[%u]\n", chunk_list_size,
		  page_cnt);

	/* allocate chunk buffers: */
	for (i = 0; i < chunk_list_size; i++) {
		chunk_list->chunks[i].buf = kzalloc(EFA_CHUNK_SIZE, GFP_KERNEL);
		if (!chunk_list->chunks[i].buf)
			goto chunk_list_dealloc;

		chunk_list->chunks[i].length = EFA_CHUNK_USED_SIZE;
	}
	chunk_list->chunks[chunk_list_size - 1].length =
		((page_cnt % EFA_PTRS_PER_CHUNK) * EFA_CHUNK_PAYLOAD_PTR_SIZE) +
			EFA_CHUNK_PTR_SIZE;

	/* fill the dma addresses of sg list pages to chunks: */
	chunk_idx = 0;
	payload_idx = 0;
	cur_chunk_buf = chunk_list->chunks[0].buf;
	rdma_for_each_block(pages_sgl, &biter, sg_dma_cnt,
			    EFA_CHUNK_PAYLOAD_SIZE) {
		cur_chunk_buf[payload_idx++] =
			rdma_block_iter_dma_address(&biter);

		if (payload_idx == EFA_PTRS_PER_CHUNK) {
			chunk_idx++;
			cur_chunk_buf = chunk_list->chunks[chunk_idx].buf;
			payload_idx = 0;
		}
	}

	/* map chunks to dma and fill chunks next ptrs */
	for (i = chunk_list_size - 1; i >= 0; i--) {
		dma_addr = dma_map_single(&dev->pdev->dev,
					  chunk_list->chunks[i].buf,
					  chunk_list->chunks[i].length,
					  DMA_TO_DEVICE);
		if (dma_mapping_error(&dev->pdev->dev, dma_addr)) {
			ibdev_err(&dev->ibdev,
				  "chunk[%u] dma_map_failed\n", i);
			goto chunk_list_unmap;
		}

		chunk_list->chunks[i].dma_addr = dma_addr;
		ibdev_dbg(&dev->ibdev,
			  "chunk[%u] mapped at [%pad]\n", i, &dma_addr);

		if (!i)
			break;

		prev_chunk_buf = chunk_list->chunks[i - 1].buf;

		ctrl_buf = (struct efa_com_ctrl_buff_info *)
				&prev_chunk_buf[EFA_PTRS_PER_CHUNK];
		ctrl_buf->length = chunk_list->chunks[i].length;

		efa_com_set_dma_addr(dma_addr,
				     &ctrl_buf->address.mem_addr_high,
				     &ctrl_buf->address.mem_addr_low);
	}

	return 0;

chunk_list_unmap:
	for (; i < chunk_list_size; i++) {
		dma_unmap_single(&dev->pdev->dev, chunk_list->chunks[i].dma_addr,
				 chunk_list->chunks[i].length, DMA_TO_DEVICE);
	}
chunk_list_dealloc:
	for (i = 0; i < chunk_list_size; i++)
		kfree(chunk_list->chunks[i].buf);

	kfree(chunk_list->chunks);
	return -ENOMEM;
}

static void pbl_chunk_list_destroy(struct efa_dev *dev, struct pbl_context *pbl)
{
	struct pbl_chunk_list *chunk_list = &pbl->phys.indirect.chunk_list;
	int i;

	for (i = 0; i < chunk_list->size; i++) {
		dma_unmap_single(&dev->pdev->dev, chunk_list->chunks[i].dma_addr,
				 chunk_list->chunks[i].length, DMA_TO_DEVICE);
		kfree(chunk_list->chunks[i].buf);
	}

	kfree(chunk_list->chunks);
}

/* initialize pbl continuous mode: map pbl buffer to a dma address. */
static int pbl_continuous_initialize(struct efa_dev *dev,
				     struct pbl_context *pbl)
{
	dma_addr_t dma_addr;

	dma_addr = dma_map_single(&dev->pdev->dev, pbl->pbl_buf,
				  pbl->pbl_buf_size_in_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(&dev->pdev->dev, dma_addr)) {
		ibdev_err(&dev->ibdev, "Unable to map pbl to DMA address\n");
		return -ENOMEM;
	}

	pbl->phys.continuous.dma_addr = dma_addr;
	ibdev_dbg(&dev->ibdev,
		  "pbl continuous - dma_addr = %pad, size[%u]\n",
		  &dma_addr, pbl->pbl_buf_size_in_bytes);

	return 0;
}

/*
 * initialize pbl indirect mode:
 * create a chunk list out of the dma addresses of the physical pages of
 * pbl buffer.
 */
static int pbl_indirect_initialize(struct efa_dev *dev, struct pbl_context *pbl)
{
	u32 size_in_pages = DIV_ROUND_UP(pbl->pbl_buf_size_in_bytes, PAGE_SIZE);
	struct scatterlist *sgl;
	int sg_dma_cnt, err;

	BUILD_BUG_ON(EFA_CHUNK_PAYLOAD_SIZE > PAGE_SIZE);
	sgl = efa_vmalloc_buf_to_sg(pbl->pbl_buf, size_in_pages);
	if (!sgl)
		return -ENOMEM;

	sg_dma_cnt = dma_map_sg(&dev->pdev->dev, sgl, size_in_pages, DMA_TO_DEVICE);
	if (!sg_dma_cnt) {
		err = -EINVAL;
		goto err_map;
	}

	pbl->phys.indirect.pbl_buf_size_in_pages = size_in_pages;
	pbl->phys.indirect.sgl = sgl;
	pbl->phys.indirect.sg_dma_cnt = sg_dma_cnt;
	err = pbl_chunk_list_create(dev, pbl);
	if (err) {
		ibdev_dbg(&dev->ibdev,
			  "chunk_list creation failed[%d]\n", err);
		goto err_chunk;
	}

	ibdev_dbg(&dev->ibdev,
		  "pbl indirect - size[%u], chunks[%u]\n",
		  pbl->pbl_buf_size_in_bytes,
		  pbl->phys.indirect.chunk_list.size);

	return 0;

err_chunk:
	dma_unmap_sg(&dev->pdev->dev, sgl, size_in_pages, DMA_TO_DEVICE);
err_map:
	kfree(sgl);
	return err;
}

static void pbl_indirect_terminate(struct efa_dev *dev, struct pbl_context *pbl)
{
	pbl_chunk_list_destroy(dev, pbl);
	dma_unmap_sg(&dev->pdev->dev, pbl->phys.indirect.sgl,
		     pbl->phys.indirect.pbl_buf_size_in_pages, DMA_TO_DEVICE);
	kfree(pbl->phys.indirect.sgl);
}

/* create a page buffer list from a mapped user memory region */
static int pbl_create(struct efa_dev *dev,
		      struct pbl_context *pbl,
		      struct ib_umem *umem,
		      int hp_cnt,
		      u8 hp_shift)
{
	int err;

	pbl->pbl_buf_size_in_bytes = hp_cnt * EFA_CHUNK_PAYLOAD_PTR_SIZE;
	pbl->pbl_buf = kvzalloc(pbl->pbl_buf_size_in_bytes, GFP_KERNEL);
	if (!pbl->pbl_buf)
		return -ENOMEM;

	if (is_vmalloc_addr(pbl->pbl_buf)) {
		pbl->physically_continuous = 0;
		err = umem_to_page_list(dev, umem, pbl->pbl_buf, hp_cnt,
					hp_shift);
		if (err)
			goto err_free;

		err = pbl_indirect_initialize(dev, pbl);
		if (err)
			goto err_free;
	} else {
		pbl->physically_continuous = 1;
		err = umem_to_page_list(dev, umem, pbl->pbl_buf, hp_cnt,
					hp_shift);
		if (err)
			goto err_free;

		err = pbl_continuous_initialize(dev, pbl);
		if (err)
			goto err_free;
	}

	ibdev_dbg(&dev->ibdev,
		  "user_pbl_created: user_pages[%u], continuous[%u]\n",
		  hp_cnt, pbl->physically_continuous);

	return 0;

err_free:
	kvfree(pbl->pbl_buf);
	return err;
}

static void pbl_destroy(struct efa_dev *dev, struct pbl_context *pbl)
{
	if (pbl->physically_continuous)
		dma_unmap_single(&dev->pdev->dev, pbl->phys.continuous.dma_addr,
				 pbl->pbl_buf_size_in_bytes, DMA_TO_DEVICE);
	else
		pbl_indirect_terminate(dev, pbl);

	kvfree(pbl->pbl_buf);
}

static int efa_create_inline_pbl(struct efa_dev *dev, struct efa_mr *mr,
				 struct efa_com_reg_mr_params *params)
{
	int err;

	params->inline_pbl = 1;
	err = umem_to_page_list(dev, mr->umem, params->pbl.inline_pbl_array,
				params->page_num, params->page_shift);
	if (err)
		return err;

	ibdev_dbg(&dev->ibdev,
		  "inline_pbl_array - pages[%u]\n", params->page_num);

	return 0;
}

static int efa_create_pbl(struct efa_dev *dev,
			  struct pbl_context *pbl,
			  struct efa_mr *mr,
			  struct efa_com_reg_mr_params *params)
{
	int err;

	err = pbl_create(dev, pbl, mr->umem, params->page_num,
			 params->page_shift);
	if (err) {
		ibdev_dbg(&dev->ibdev, "Failed to create pbl[%d]\n", err);
		return err;
	}

	params->inline_pbl = 0;
	params->indirect = !pbl->physically_continuous;
	if (pbl->physically_continuous) {
		params->pbl.pbl.length = pbl->pbl_buf_size_in_bytes;

		efa_com_set_dma_addr(pbl->phys.continuous.dma_addr,
				     &params->pbl.pbl.address.mem_addr_high,
				     &params->pbl.pbl.address.mem_addr_low);
	} else {
		params->pbl.pbl.length =
			pbl->phys.indirect.chunk_list.chunks[0].length;

		efa_com_set_dma_addr(pbl->phys.indirect.chunk_list.chunks[0].dma_addr,
				     &params->pbl.pbl.address.mem_addr_high,
				     &params->pbl.pbl.address.mem_addr_low);
	}

	return 0;
}

static struct efa_mr *efa_alloc_mr(struct ib_pd *ibpd, int access_flags,
				   struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	int supp_access_flags;
	struct efa_mr *mr;

	if (udata && udata->inlen &&
	    !ib_is_udata_cleared(udata, 0, sizeof(udata->inlen))) {
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, udata not cleared\n");
		return ERR_PTR(-EINVAL);
	}

	supp_access_flags =
		IB_ACCESS_LOCAL_WRITE |
		(EFA_DEV_CAP(dev, RDMA_READ) ? IB_ACCESS_REMOTE_READ : 0);

	access_flags &= ~IB_ACCESS_OPTIONAL;
	if (access_flags & ~supp_access_flags) {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported access flags[%#x], supported[%#x]\n",
			  access_flags, supp_access_flags);
		return ERR_PTR(-EOPNOTSUPP);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	return mr;
}

static int efa_register_mr(struct ib_pd *ibpd, struct efa_mr *mr, u64 start,
			   u64 length, u64 virt_addr, int access_flags)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_com_reg_mr_params params = {};
	struct efa_com_reg_mr_result result = {};
	struct pbl_context pbl;
	unsigned int pg_sz;
	int inline_size;
	int err;

	params.pd = to_epd(ibpd)->pdn;
	params.iova = virt_addr;
	params.mr_length_in_bytes = length;
	params.permissions = access_flags;

	pg_sz = ib_umem_find_best_pgsz(mr->umem,
				       dev->dev_attr.page_size_cap,
				       virt_addr);
	if (!pg_sz) {
		ibdev_dbg(&dev->ibdev, "Failed to find a suitable page size in page_size_cap %#llx\n",
			  dev->dev_attr.page_size_cap);
		return -EOPNOTSUPP;
	}

	params.page_shift = order_base_2(pg_sz);
	params.page_num = ib_umem_num_dma_blocks(mr->umem, pg_sz);

	ibdev_dbg(&dev->ibdev,
		  "start %#llx length %#llx params.page_shift %u params.page_num %u\n",
		  start, length, params.page_shift, params.page_num);

	inline_size = ARRAY_SIZE(params.pbl.inline_pbl_array);
	if (params.page_num <= inline_size) {
		err = efa_create_inline_pbl(dev, mr, &params);
		if (err)
			return err;

		err = efa_com_register_mr(&dev->edev, &params, &result);
		if (err)
			return err;
	} else {
		err = efa_create_pbl(dev, &pbl, mr, &params);
		if (err)
			return err;

		err = efa_com_register_mr(&dev->edev, &params, &result);
		pbl_destroy(dev, &pbl);

		if (err)
			return err;
	}

	mr->ibmr.lkey = result.l_key;
	mr->ibmr.rkey = result.r_key;
	mr->ibmr.length = length;
	ibdev_dbg(&dev->ibdev, "Registered mr[%d]\n", mr->ibmr.lkey);

	return 0;
}

struct ib_mr *efa_reg_user_mr_dmabuf(struct ib_pd *ibpd, u64 start,
				     u64 length, u64 virt_addr,
				     int fd, int access_flags,
				     struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct ib_umem_dmabuf *umem_dmabuf;
	struct efa_mr *mr;
	int err;

	mr = efa_alloc_mr(ibpd, access_flags, udata);
	if (IS_ERR(mr)) {
		err = PTR_ERR(mr);
		goto err_out;
	}

	umem_dmabuf = ib_umem_dmabuf_get_pinned(ibpd->device, start, length, fd,
						access_flags);
	if (IS_ERR(umem_dmabuf)) {
		err = PTR_ERR(umem_dmabuf);
		ibdev_dbg(&dev->ibdev, "Failed to get dmabuf umem[%d]\n", err);
		goto err_free;
	}

	mr->umem = &umem_dmabuf->umem;
	err = efa_register_mr(ibpd, mr, start, length, virt_addr, access_flags);
	if (err)
		goto err_release;

	return &mr->ibmr;

err_release:
	ib_umem_release(mr->umem);
err_free:
	kfree(mr);
err_out:
	atomic64_inc(&dev->stats.reg_mr_err);
	return ERR_PTR(err);
}

struct ib_mr *efa_reg_mr(struct ib_pd *ibpd, u64 start, u64 length,
			 u64 virt_addr, int access_flags,
			 struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_mr *mr;
	int err;

	mr = efa_alloc_mr(ibpd, access_flags, udata);
	if (IS_ERR(mr)) {
		err = PTR_ERR(mr);
		goto err_out;
	}

	mr->umem = ib_umem_get(ibpd->device, start, length, access_flags);
	if (IS_ERR(mr->umem)) {
		err = PTR_ERR(mr->umem);
		ibdev_dbg(&dev->ibdev,
			  "Failed to pin and map user space memory[%d]\n", err);
		goto err_free;
	}

	err = efa_register_mr(ibpd, mr, start, length, virt_addr, access_flags);
	if (err)
		goto err_release;

	return &mr->ibmr;

err_release:
	ib_umem_release(mr->umem);
err_free:
	kfree(mr);
err_out:
	atomic64_inc(&dev->stats.reg_mr_err);
	return ERR_PTR(err);
}

int efa_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibmr->device);
	struct efa_com_dereg_mr_params params;
	struct efa_mr *mr = to_emr(ibmr);
	int err;

	ibdev_dbg(&dev->ibdev, "Deregister mr[%d]\n", ibmr->lkey);

	params.l_key = mr->ibmr.lkey;
	err = efa_com_dereg_mr(&dev->edev, &params);
	if (err)
		return err;

	ib_umem_release(mr->umem);
	kfree(mr);

	return 0;
}

int efa_get_port_immutable(struct ib_device *ibdev, u32 port_num,
			   struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err) {
		ibdev_dbg(ibdev, "Couldn't query port err[%d]\n", err);
		return err;
	}

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;

	return 0;
}

static int efa_dealloc_uar(struct efa_dev *dev, u16 uarn)
{
	struct efa_com_dealloc_uar_params params = {
		.uarn = uarn,
	};

	return efa_com_dealloc_uar(&dev->edev, &params);
}

#define EFA_CHECK_USER_COMP(_dev, _comp_mask, _attr, _mask, _attr_str) \
	(_attr_str = (!(_dev)->dev_attr._attr || ((_comp_mask) & (_mask))) ? \
		     NULL : #_attr)

static int efa_user_comp_handshake(const struct ib_ucontext *ibucontext,
				   const struct efa_ibv_alloc_ucontext_cmd *cmd)
{
	struct efa_dev *dev = to_edev(ibucontext->device);
	char *attr_str;

	if (EFA_CHECK_USER_COMP(dev, cmd->comp_mask, max_tx_batch,
				EFA_ALLOC_UCONTEXT_CMD_COMP_TX_BATCH, attr_str))
		goto err;

	if (EFA_CHECK_USER_COMP(dev, cmd->comp_mask, min_sq_depth,
				EFA_ALLOC_UCONTEXT_CMD_COMP_MIN_SQ_WR,
				attr_str))
		goto err;

	return 0;

err:
	ibdev_dbg(&dev->ibdev, "Userspace handshake failed for %s attribute\n",
		  attr_str);
	return -EOPNOTSUPP;
}

int efa_alloc_ucontext(struct ib_ucontext *ibucontext, struct ib_udata *udata)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);
	struct efa_ibv_alloc_ucontext_resp resp = {};
	struct efa_ibv_alloc_ucontext_cmd cmd = {};
	struct efa_com_alloc_uar_result result;
	int err;

	/*
	 * it's fine if the driver does not know all request fields,
	 * we will ack input fields in our response.
	 */

	err = ib_copy_from_udata(&cmd, udata,
				 min(sizeof(cmd), udata->inlen));
	if (err) {
		ibdev_dbg(&dev->ibdev,
			  "Cannot copy udata for alloc_ucontext\n");
		goto err_out;
	}

	err = efa_user_comp_handshake(ibucontext, &cmd);
	if (err)
		goto err_out;

	err = efa_com_alloc_uar(&dev->edev, &result);
	if (err)
		goto err_out;

	ucontext->uarn = result.uarn;

	resp.cmds_supp_udata_mask |= EFA_USER_CMDS_SUPP_UDATA_QUERY_DEVICE;
	resp.cmds_supp_udata_mask |= EFA_USER_CMDS_SUPP_UDATA_CREATE_AH;
	resp.sub_cqs_per_cq = dev->dev_attr.sub_cqs_per_cq;
	resp.inline_buf_size = dev->dev_attr.inline_buf_size;
	resp.max_llq_size = dev->dev_attr.max_llq_size;
	resp.max_tx_batch = dev->dev_attr.max_tx_batch;
	resp.min_sq_wr = dev->dev_attr.min_sq_depth;

	err = ib_copy_to_udata(udata, &resp,
			       min(sizeof(resp), udata->outlen));
	if (err)
		goto err_dealloc_uar;

	return 0;

err_dealloc_uar:
	efa_dealloc_uar(dev, result.uarn);
err_out:
	atomic64_inc(&dev->stats.alloc_ucontext_err);
	return err;
}

void efa_dealloc_ucontext(struct ib_ucontext *ibucontext)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);

	efa_dealloc_uar(dev, ucontext->uarn);
}

void efa_mmap_free(struct rdma_user_mmap_entry *rdma_entry)
{
	struct efa_user_mmap_entry *entry = to_emmap(rdma_entry);

	kfree(entry);
}

static int __efa_mmap(struct efa_dev *dev, struct efa_ucontext *ucontext,
		      struct vm_area_struct *vma)
{
	struct rdma_user_mmap_entry *rdma_entry;
	struct efa_user_mmap_entry *entry;
	unsigned long va;
	int err = 0;
	u64 pfn;

	rdma_entry = rdma_user_mmap_entry_get(&ucontext->ibucontext, vma);
	if (!rdma_entry) {
		ibdev_dbg(&dev->ibdev,
			  "pgoff[%#lx] does not have valid entry\n",
			  vma->vm_pgoff);
		atomic64_inc(&dev->stats.mmap_err);
		return -EINVAL;
	}
	entry = to_emmap(rdma_entry);

	ibdev_dbg(&dev->ibdev,
		  "Mapping address[%#llx], length[%#zx], mmap_flag[%d]\n",
		  entry->address, rdma_entry->npages * PAGE_SIZE,
		  entry->mmap_flag);

	pfn = entry->address >> PAGE_SHIFT;
	switch (entry->mmap_flag) {
	case EFA_MMAP_IO_NC:
		err = rdma_user_mmap_io(&ucontext->ibucontext, vma, pfn,
					entry->rdma_entry.npages * PAGE_SIZE,
					pgprot_noncached(vma->vm_page_prot),
					rdma_entry);
		break;
	case EFA_MMAP_IO_WC:
		err = rdma_user_mmap_io(&ucontext->ibucontext, vma, pfn,
					entry->rdma_entry.npages * PAGE_SIZE,
					pgprot_writecombine(vma->vm_page_prot),
					rdma_entry);
		break;
	case EFA_MMAP_DMA_PAGE:
		for (va = vma->vm_start; va < vma->vm_end;
		     va += PAGE_SIZE, pfn++) {
			err = vm_insert_page(vma, va, pfn_to_page(pfn));
			if (err)
				break;
		}
		break;
	default:
		err = -EINVAL;
	}

	if (err) {
		ibdev_dbg(
			&dev->ibdev,
			"Couldn't mmap address[%#llx] length[%#zx] mmap_flag[%d] err[%d]\n",
			entry->address, rdma_entry->npages * PAGE_SIZE,
			entry->mmap_flag, err);
		atomic64_inc(&dev->stats.mmap_err);
	}

	rdma_user_mmap_entry_put(rdma_entry);
	return err;
}

int efa_mmap(struct ib_ucontext *ibucontext,
	     struct vm_area_struct *vma)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);
	size_t length = vma->vm_end - vma->vm_start;

	ibdev_dbg(&dev->ibdev,
		  "start %#lx, end %#lx, length = %#zx, pgoff = %#lx\n",
		  vma->vm_start, vma->vm_end, length, vma->vm_pgoff);

	return __efa_mmap(dev, ucontext, vma);
}

static int efa_ah_destroy(struct efa_dev *dev, struct efa_ah *ah)
{
	struct efa_com_destroy_ah_params params = {
		.ah = ah->ah,
		.pdn = to_epd(ah->ibah.pd)->pdn,
	};

	return efa_com_destroy_ah(&dev->edev, &params);
}

int efa_create_ah(struct ib_ah *ibah,
		  struct rdma_ah_init_attr *init_attr,
		  struct ib_udata *udata)
{
	struct rdma_ah_attr *ah_attr = init_attr->ah_attr;
	struct efa_dev *dev = to_edev(ibah->device);
	struct efa_com_create_ah_params params = {};
	struct efa_ibv_create_ah_resp resp = {};
	struct efa_com_create_ah_result result;
	struct efa_ah *ah = to_eah(ibah);
	int err;

	if (!(init_attr->flags & RDMA_CREATE_AH_SLEEPABLE)) {
		ibdev_dbg(&dev->ibdev,
			  "Create address handle is not supported in atomic context\n");
		err = -EOPNOTSUPP;
		goto err_out;
	}

	if (udata->inlen &&
	    !ib_is_udata_cleared(udata, 0, udata->inlen)) {
		ibdev_dbg(&dev->ibdev, "Incompatible ABI params\n");
		err = -EINVAL;
		goto err_out;
	}

	memcpy(params.dest_addr, ah_attr->grh.dgid.raw,
	       sizeof(params.dest_addr));
	params.pdn = to_epd(ibah->pd)->pdn;
	err = efa_com_create_ah(&dev->edev, &params, &result);
	if (err)
		goto err_out;

	memcpy(ah->id, ah_attr->grh.dgid.raw, sizeof(ah->id));
	ah->ah = result.ah;

	resp.efa_address_handle = result.ah;

	if (udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(&dev->ibdev,
				  "Failed to copy udata for create_ah response\n");
			goto err_destroy_ah;
		}
	}
	ibdev_dbg(&dev->ibdev, "Created ah[%d]\n", ah->ah);

	return 0;

err_destroy_ah:
	efa_ah_destroy(dev, ah);
err_out:
	atomic64_inc(&dev->stats.create_ah_err);
	return err;
}

int efa_destroy_ah(struct ib_ah *ibah, u32 flags)
{
	struct efa_dev *dev = to_edev(ibah->pd->device);
	struct efa_ah *ah = to_eah(ibah);

	ibdev_dbg(&dev->ibdev, "Destroy ah[%d]\n", ah->ah);

	if (!(flags & RDMA_DESTROY_AH_SLEEPABLE)) {
		ibdev_dbg(&dev->ibdev,
			  "Destroy address handle is not supported in atomic context\n");
		return -EOPNOTSUPP;
	}

	efa_ah_destroy(dev, ah);
	return 0;
}

struct rdma_hw_stats *efa_alloc_hw_port_stats(struct ib_device *ibdev,
					      u32 port_num)
{
	return rdma_alloc_hw_stats_struct(efa_port_stats_descs,
					  ARRAY_SIZE(efa_port_stats_descs),
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

struct rdma_hw_stats *efa_alloc_hw_device_stats(struct ib_device *ibdev)
{
	return rdma_alloc_hw_stats_struct(efa_device_stats_descs,
					  ARRAY_SIZE(efa_device_stats_descs),
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

static int efa_fill_device_stats(struct efa_dev *dev,
				 struct rdma_hw_stats *stats)
{
	struct efa_com_stats_admin *as = &dev->edev.aq.stats;
	struct efa_stats *s = &dev->stats;

	stats->value[EFA_SUBMITTED_CMDS] = atomic64_read(&as->submitted_cmd);
	stats->value[EFA_COMPLETED_CMDS] = atomic64_read(&as->completed_cmd);
	stats->value[EFA_CMDS_ERR] = atomic64_read(&as->cmd_err);
	stats->value[EFA_NO_COMPLETION_CMDS] = atomic64_read(&as->no_completion);

	stats->value[EFA_KEEP_ALIVE_RCVD] = atomic64_read(&s->keep_alive_rcvd);
	stats->value[EFA_ALLOC_PD_ERR] = atomic64_read(&s->alloc_pd_err);
	stats->value[EFA_CREATE_QP_ERR] = atomic64_read(&s->create_qp_err);
	stats->value[EFA_CREATE_CQ_ERR] = atomic64_read(&s->create_cq_err);
	stats->value[EFA_REG_MR_ERR] = atomic64_read(&s->reg_mr_err);
	stats->value[EFA_ALLOC_UCONTEXT_ERR] =
		atomic64_read(&s->alloc_ucontext_err);
	stats->value[EFA_CREATE_AH_ERR] = atomic64_read(&s->create_ah_err);
	stats->value[EFA_MMAP_ERR] = atomic64_read(&s->mmap_err);

	return ARRAY_SIZE(efa_device_stats_descs);
}

static int efa_fill_port_stats(struct efa_dev *dev, struct rdma_hw_stats *stats,
			       u32 port_num)
{
	struct efa_com_get_stats_params params = {};
	union efa_com_get_stats_result result;
	struct efa_com_rdma_read_stats *rrs;
	struct efa_com_messages_stats *ms;
	struct efa_com_basic_stats *bs;
	int err;

	params.scope = EFA_ADMIN_GET_STATS_SCOPE_ALL;
	params.type = EFA_ADMIN_GET_STATS_TYPE_BASIC;

	err = efa_com_get_stats(&dev->edev, &params, &result);
	if (err)
		return err;

	bs = &result.basic_stats;
	stats->value[EFA_TX_BYTES] = bs->tx_bytes;
	stats->value[EFA_TX_PKTS] = bs->tx_pkts;
	stats->value[EFA_RX_BYTES] = bs->rx_bytes;
	stats->value[EFA_RX_PKTS] = bs->rx_pkts;
	stats->value[EFA_RX_DROPS] = bs->rx_drops;

	params.type = EFA_ADMIN_GET_STATS_TYPE_MESSAGES;
	err = efa_com_get_stats(&dev->edev, &params, &result);
	if (err)
		return err;

	ms = &result.messages_stats;
	stats->value[EFA_SEND_BYTES] = ms->send_bytes;
	stats->value[EFA_SEND_WRS] = ms->send_wrs;
	stats->value[EFA_RECV_BYTES] = ms->recv_bytes;
	stats->value[EFA_RECV_WRS] = ms->recv_wrs;

	params.type = EFA_ADMIN_GET_STATS_TYPE_RDMA_READ;
	err = efa_com_get_stats(&dev->edev, &params, &result);
	if (err)
		return err;

	rrs = &result.rdma_read_stats;
	stats->value[EFA_RDMA_READ_WRS] = rrs->read_wrs;
	stats->value[EFA_RDMA_READ_BYTES] = rrs->read_bytes;
	stats->value[EFA_RDMA_READ_WR_ERR] = rrs->read_wr_err;
	stats->value[EFA_RDMA_READ_RESP_BYTES] = rrs->read_resp_bytes;

	return ARRAY_SIZE(efa_port_stats_descs);
}

int efa_get_hw_stats(struct ib_device *ibdev, struct rdma_hw_stats *stats,
		     u32 port_num, int index)
{
	if (port_num)
		return efa_fill_port_stats(to_edev(ibdev), stats, port_num);
	else
		return efa_fill_device_stats(to_edev(ibdev), stats);
}

enum rdma_link_layer efa_port_link_layer(struct ib_device *ibdev,
					 u32 port_num)
{
	return IB_LINK_LAYER_UNSPECIFIED;
}

