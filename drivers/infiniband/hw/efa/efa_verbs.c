// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/vmalloc.h>

#include <rdma/ib_addr.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#include <rdma/uverbs_ioctl.h>

#include "efa.h"

#define EFA_MMAP_FLAG_SHIFT 56
#define EFA_MMAP_PAGE_MASK GENMASK(EFA_MMAP_FLAG_SHIFT - 1, 0)
#define EFA_MMAP_INVALID U64_MAX

enum {
	EFA_MMAP_DMA_PAGE = 0,
	EFA_MMAP_IO_WC,
	EFA_MMAP_IO_NC,
};

#define EFA_AENQ_ENABLED_GROUPS \
	(BIT(EFA_ADMIN_FATAL_ERROR) | BIT(EFA_ADMIN_WARNING) | \
	 BIT(EFA_ADMIN_NOTIFICATION) | BIT(EFA_ADMIN_KEEP_ALIVE))

struct efa_mmap_entry {
	void  *obj;
	u64 address;
	u64 length;
	u32 mmap_page;
	u8 mmap_flag;
};

static inline u64 get_mmap_key(const struct efa_mmap_entry *efa)
{
	return ((u64)efa->mmap_flag << EFA_MMAP_FLAG_SHIFT) |
	       ((u64)efa->mmap_page << PAGE_SHIFT);
}

#define EFA_DEFINE_STATS(op) \
	op(EFA_TX_BYTES, "tx_bytes") \
	op(EFA_TX_PKTS, "tx_pkts") \
	op(EFA_RX_BYTES, "rx_bytes") \
	op(EFA_RX_PKTS, "rx_pkts") \
	op(EFA_RX_DROPS, "rx_drops") \
	op(EFA_SUBMITTED_CMDS, "submitted_cmds") \
	op(EFA_COMPLETED_CMDS, "completed_cmds") \
	op(EFA_NO_COMPLETION_CMDS, "no_completion_cmds") \
	op(EFA_KEEP_ALIVE_RCVD, "keep_alive_rcvd") \
	op(EFA_ALLOC_PD_ERR, "alloc_pd_err") \
	op(EFA_CREATE_QP_ERR, "create_qp_err") \
	op(EFA_REG_MR_ERR, "reg_mr_err") \
	op(EFA_ALLOC_UCONTEXT_ERR, "alloc_ucontext_err") \
	op(EFA_CREATE_AH_ERR, "create_ah_err")

#define EFA_STATS_ENUM(ename, name) ename,
#define EFA_STATS_STR(ename, name) [ename] = name,

enum efa_hw_stats {
	EFA_DEFINE_STATS(EFA_STATS_ENUM)
};

static const char *const efa_stats_names[] = {
	EFA_DEFINE_STATS(EFA_STATS_STR)
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

#define EFA_SUPPORTED_ACCESS_FLAGS IB_ACCESS_LOCAL_WRITE

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

#define field_avail(x, fld, sz) (offsetof(typeof(x), fld) + \
				 FIELD_SIZEOF(typeof(x), fld) <= (sz))

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

/*
 * This is only called when the ucontext is destroyed and there can be no
 * concurrent query via mmap or allocate on the xarray, thus we can be sure no
 * other thread is using the entry pointer. We also know that all the BAR
 * pages have either been zap'd or munmaped at this point.  Normal pages are
 * refcounted and will be freed at the proper time.
 */
static void mmap_entries_remove_free(struct efa_dev *dev,
				     struct efa_ucontext *ucontext)
{
	struct efa_mmap_entry *entry;
	unsigned long mmap_page;

	xa_for_each(&ucontext->mmap_xa, mmap_page, entry) {
		xa_erase(&ucontext->mmap_xa, mmap_page);

		ibdev_dbg(
			&dev->ibdev,
			"mmap: obj[0x%p] key[%#llx] addr[%#llx] len[%#llx] removed\n",
			entry->obj, get_mmap_key(entry), entry->address,
			entry->length);
		if (entry->mmap_flag == EFA_MMAP_DMA_PAGE)
			/* DMA mapping is already gone, now free the pages */
			free_pages_exact(phys_to_virt(entry->address),
					 entry->length);
		kfree(entry);
	}
}

static struct efa_mmap_entry *mmap_entry_get(struct efa_dev *dev,
					     struct efa_ucontext *ucontext,
					     u64 key, u64 len)
{
	struct efa_mmap_entry *entry;
	u64 mmap_page;

	mmap_page = (key & EFA_MMAP_PAGE_MASK) >> PAGE_SHIFT;
	if (mmap_page > U32_MAX)
		return NULL;

	entry = xa_load(&ucontext->mmap_xa, mmap_page);
	if (!entry || get_mmap_key(entry) != key || entry->length != len)
		return NULL;

	ibdev_dbg(&dev->ibdev,
		  "mmap: obj[0x%p] key[%#llx] addr[%#llx] len[%#llx] removed\n",
		  entry->obj, key, entry->address, entry->length);

	return entry;
}

/*
 * Note this locking scheme cannot support removal of entries, except during
 * ucontext destruction when the core code guarentees no concurrency.
 */
static u64 mmap_entry_insert(struct efa_dev *dev, struct efa_ucontext *ucontext,
			     void *obj, u64 address, u64 length, u8 mmap_flag)
{
	struct efa_mmap_entry *entry;
	u32 next_mmap_page;
	int err;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return EFA_MMAP_INVALID;

	entry->obj = obj;
	entry->address = address;
	entry->length = length;
	entry->mmap_flag = mmap_flag;

	xa_lock(&ucontext->mmap_xa);
	if (check_add_overflow(ucontext->mmap_xa_page,
			       (u32)(length >> PAGE_SHIFT),
			       &next_mmap_page))
		goto err_unlock;

	entry->mmap_page = ucontext->mmap_xa_page;
	ucontext->mmap_xa_page = next_mmap_page;
	err = __xa_insert(&ucontext->mmap_xa, entry->mmap_page, entry,
			  GFP_KERNEL);
	if (err)
		goto err_unlock;

	xa_unlock(&ucontext->mmap_xa);

	ibdev_dbg(
		&dev->ibdev,
		"mmap: obj[0x%p] addr[%#llx], len[%#llx], key[%#llx] inserted\n",
		entry->obj, entry->address, entry->length, get_mmap_key(entry));

	return get_mmap_key(entry);

err_unlock:
	xa_unlock(&ucontext->mmap_xa);
	kfree(entry);
	return EFA_MMAP_INVALID;

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

	if (udata && udata->outlen) {
		resp.max_sq_sge = dev_attr->max_sq_sge;
		resp.max_rq_sge = dev_attr->max_rq_sge;
		resp.max_sq_wr = dev_attr->max_sq_depth;
		resp.max_rq_wr = dev_attr->max_rq_depth;

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

int efa_query_port(struct ib_device *ibdev, u8 port,
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
	props->max_mtu = ib_mtu_int_to_enum(dev->mtu);
	props->active_mtu = ib_mtu_int_to_enum(dev->mtu);
	props->max_msg_sz = dev->mtu;
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
	 IB_QP_QKEY | IB_QP_SQ_PSN | IB_QP_CAP)

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

int efa_query_gid(struct ib_device *ibdev, u8 port, int index,
		  union ib_gid *gid)
{
	struct efa_dev *dev = to_edev(ibdev);

	memcpy(gid->raw, dev->addr, sizeof(dev->addr));

	return 0;
}

int efa_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
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
	atomic64_inc(&dev->stats.sw_stats.alloc_pd_err);
	return err;
}

void efa_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_pd *pd = to_epd(ibpd);

	ibdev_dbg(&dev->ibdev, "Dealloc pd[%d]\n", pd->pdn);
	efa_pd_dealloc(dev, pd->pdn);
}

static int efa_destroy_qp_handle(struct efa_dev *dev, u32 qp_handle)
{
	struct efa_com_destroy_qp_params params = { .qp_handle = qp_handle };

	return efa_com_destroy_qp(&dev->edev, &params);
}

int efa_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibqp->pd->device);
	struct efa_qp *qp = to_eqp(ibqp);
	int err;

	ibdev_dbg(&dev->ibdev, "Destroy qp[%u]\n", ibqp->qp_num);
	err = efa_destroy_qp_handle(dev, qp->qp_handle);
	if (err)
		return err;

	if (qp->rq_cpu_addr) {
		ibdev_dbg(&dev->ibdev,
			  "qp->cpu_addr[0x%p] freed: size[%lu], dma[%pad]\n",
			  qp->rq_cpu_addr, qp->rq_size,
			  &qp->rq_dma_addr);
		dma_unmap_single(&dev->pdev->dev, qp->rq_dma_addr, qp->rq_size,
				 DMA_TO_DEVICE);
	}

	kfree(qp);
	return 0;
}

static int qp_mmap_entries_setup(struct efa_qp *qp,
				 struct efa_dev *dev,
				 struct efa_ucontext *ucontext,
				 struct efa_com_create_qp_params *params,
				 struct efa_ibv_create_qp_resp *resp)
{
	/*
	 * Once an entry is inserted it might be mmapped, hence cannot be
	 * cleaned up until dealloc_ucontext.
	 */
	resp->sq_db_mmap_key =
		mmap_entry_insert(dev, ucontext, qp,
				  dev->db_bar_addr + resp->sq_db_offset,
				  PAGE_SIZE, EFA_MMAP_IO_NC);
	if (resp->sq_db_mmap_key == EFA_MMAP_INVALID)
		return -ENOMEM;

	resp->sq_db_offset &= ~PAGE_MASK;

	resp->llq_desc_mmap_key =
		mmap_entry_insert(dev, ucontext, qp,
				  dev->mem_bar_addr + resp->llq_desc_offset,
				  PAGE_ALIGN(params->sq_ring_size_in_bytes +
					     (resp->llq_desc_offset & ~PAGE_MASK)),
				  EFA_MMAP_IO_WC);
	if (resp->llq_desc_mmap_key == EFA_MMAP_INVALID)
		return -ENOMEM;

	resp->llq_desc_offset &= ~PAGE_MASK;

	if (qp->rq_size) {
		resp->rq_db_mmap_key =
			mmap_entry_insert(dev, ucontext, qp,
					  dev->db_bar_addr + resp->rq_db_offset,
					  PAGE_SIZE, EFA_MMAP_IO_NC);
		if (resp->rq_db_mmap_key == EFA_MMAP_INVALID)
			return -ENOMEM;

		resp->rq_db_offset &= ~PAGE_MASK;

		resp->rq_mmap_key =
			mmap_entry_insert(dev, ucontext, qp,
					  virt_to_phys(qp->rq_cpu_addr),
					  qp->rq_size, EFA_MMAP_DMA_PAGE);
		if (resp->rq_mmap_key == EFA_MMAP_INVALID)
			return -ENOMEM;

		resp->rq_mmap_size = qp->rq_size;
	}

	return 0;
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

struct ib_qp *efa_create_qp(struct ib_pd *ibpd,
			    struct ib_qp_init_attr *init_attr,
			    struct ib_udata *udata)
{
	struct efa_com_create_qp_params create_qp_params = {};
	struct efa_com_create_qp_result create_qp_resp;
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_ibv_create_qp_resp resp = {};
	struct efa_ibv_create_qp cmd = {};
	bool rq_entry_inserted = false;
	struct efa_ucontext *ucontext;
	struct efa_qp *qp;
	int err;

	ucontext = rdma_udata_to_drv_context(udata, struct efa_ucontext,
					     ibucontext);

	err = efa_qp_validate_cap(dev, init_attr);
	if (err)
		goto err_out;

	err = efa_qp_validate_attr(dev, init_attr);
	if (err)
		goto err_out;

	if (!field_avail(cmd, driver_qp_type, udata->inlen)) {
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

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp) {
		err = -ENOMEM;
		goto err_out;
	}

	create_qp_params.uarn = ucontext->uarn;
	create_qp_params.pd = to_epd(ibpd)->pdn;

	if (init_attr->qp_type == IB_QPT_UD) {
		create_qp_params.qp_type = EFA_ADMIN_QP_TYPE_UD;
	} else if (cmd.driver_qp_type == EFA_QP_DRIVER_TYPE_SRD) {
		create_qp_params.qp_type = EFA_ADMIN_QP_TYPE_SRD;
	} else {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported qp type %d driver qp type %d\n",
			  init_attr->qp_type, cmd.driver_qp_type);
		err = -EOPNOTSUPP;
		goto err_free_qp;
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
			goto err_free_qp;
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

	rq_entry_inserted = true;
	qp->qp_handle = create_qp_resp.qp_handle;
	qp->ibqp.qp_num = create_qp_resp.qp_num;
	qp->ibqp.qp_type = init_attr->qp_type;
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
			goto err_destroy_qp;
		}
	}

	ibdev_dbg(&dev->ibdev, "Created qp[%d]\n", qp->ibqp.qp_num);

	return &qp->ibqp;

err_destroy_qp:
	efa_destroy_qp_handle(dev, create_qp_resp.qp_handle);
err_free_mapped:
	if (qp->rq_size) {
		dma_unmap_single(&dev->pdev->dev, qp->rq_dma_addr, qp->rq_size,
				 DMA_TO_DEVICE);
		if (!rq_entry_inserted)
			free_pages_exact(qp->rq_cpu_addr, qp->rq_size);
	}
err_free_qp:
	kfree(qp);
err_out:
	atomic64_inc(&dev->stats.sw_stats.create_qp_err);
	return ERR_PTR(err);
}

static int efa_modify_qp_validate(struct efa_dev *dev, struct efa_qp *qp,
				  struct ib_qp_attr *qp_attr, int qp_attr_mask,
				  enum ib_qp_state cur_state,
				  enum ib_qp_state new_state)
{
#define EFA_MODIFY_QP_SUPP_MASK \
	(IB_QP_STATE | IB_QP_CUR_STATE | IB_QP_EN_SQD_ASYNC_NOTIFY | \
	 IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_QKEY | IB_QP_SQ_PSN)

	if (qp_attr_mask & ~EFA_MODIFY_QP_SUPP_MASK) {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported qp_attr_mask[%#x] supported[%#x]\n",
			  qp_attr_mask, EFA_MODIFY_QP_SUPP_MASK);
		return -EOPNOTSUPP;
	}

	if (!ib_modify_qp_is_ok(cur_state, new_state, IB_QPT_UD,
				qp_attr_mask)) {
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
		params.modify_mask |= BIT(EFA_ADMIN_QP_STATE_BIT) |
				      BIT(EFA_ADMIN_CUR_QP_STATE_BIT);
		params.cur_qp_state = qp_attr->cur_qp_state;
		params.qp_state = qp_attr->qp_state;
	}

	if (qp_attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY) {
		params.modify_mask |=
			BIT(EFA_ADMIN_SQ_DRAINED_ASYNC_NOTIFY_BIT);
		params.sq_drained_async_notify = qp_attr->en_sqd_async_notify;
	}

	if (qp_attr_mask & IB_QP_QKEY) {
		params.modify_mask |= BIT(EFA_ADMIN_QKEY_BIT);
		params.qkey = qp_attr->qkey;
	}

	if (qp_attr_mask & IB_QP_SQ_PSN) {
		params.modify_mask |= BIT(EFA_ADMIN_SQ_PSN_BIT);
		params.sq_psn = qp_attr->sq_psn;
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

void efa_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibcq->device);
	struct efa_cq *cq = to_ecq(ibcq);

	ibdev_dbg(&dev->ibdev,
		  "Destroy cq[%d] virt[0x%p] freed: size[%lu], dma[%pad]\n",
		  cq->cq_idx, cq->cpu_addr, cq->size, &cq->dma_addr);

	efa_destroy_cq_idx(dev, cq->cq_idx);
	dma_unmap_single(&dev->pdev->dev, cq->dma_addr, cq->size,
			 DMA_FROM_DEVICE);
}

static int cq_mmap_entries_setup(struct efa_dev *dev, struct efa_cq *cq,
				 struct efa_ibv_create_cq_resp *resp)
{
	resp->q_mmap_size = cq->size;
	resp->q_mmap_key = mmap_entry_insert(dev, cq->ucontext, cq,
					     virt_to_phys(cq->cpu_addr),
					     cq->size, EFA_MMAP_DMA_PAGE);
	if (resp->q_mmap_key == EFA_MMAP_INVALID)
		return -ENOMEM;

	return 0;
}

int efa_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		  struct ib_udata *udata)
{
	struct efa_ucontext *ucontext = rdma_udata_to_drv_context(
		udata, struct efa_ucontext, ibucontext);
	struct efa_ibv_create_cq_resp resp = {};
	struct efa_com_create_cq_params params;
	struct efa_com_create_cq_result result;
	struct ib_device *ibdev = ibcq->device;
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_ibv_create_cq cmd = {};
	struct efa_cq *cq = to_ecq(ibcq);
	bool cq_entry_inserted = false;
	int entries = attr->cqe;
	int err;

	ibdev_dbg(ibdev, "create_cq entries %d\n", entries);

	if (entries < 1 || entries > dev->dev_attr.max_cq_depth) {
		ibdev_dbg(ibdev,
			  "cq: requested entries[%u] non-positive or greater than max[%u]\n",
			  entries, dev->dev_attr.max_cq_depth);
		err = -EINVAL;
		goto err_out;
	}

	if (!field_avail(cmd, num_sub_cqs, udata->inlen)) {
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

	if (cmd.comp_mask || !is_reserved_cleared(cmd.reserved_50)) {
		ibdev_dbg(ibdev,
			  "Incompatible ABI params, unknown fields in udata\n");
		err = -EINVAL;
		goto err_out;
	}

	if (!cmd.cq_entry_size) {
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
	err = efa_com_create_cq(&dev->edev, &params, &result);
	if (err)
		goto err_free_mapped;

	resp.cq_idx = result.cq_idx;
	cq->cq_idx = result.cq_idx;
	cq->ibcq.cqe = result.actual_depth;
	WARN_ON_ONCE(entries != result.actual_depth);

	err = cq_mmap_entries_setup(dev, cq, &resp);
	if (err) {
		ibdev_dbg(ibdev, "Could not setup cq[%u] mmap entries\n",
			  cq->cq_idx);
		goto err_destroy_cq;
	}

	cq_entry_inserted = true;

	if (udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(ibdev,
				  "Failed to copy udata for create_cq\n");
			goto err_destroy_cq;
		}
	}

	ibdev_dbg(ibdev, "Created cq[%d], cq depth[%u]. dma[%pad] virt[0x%p]\n",
		  cq->cq_idx, result.actual_depth, &cq->dma_addr, cq->cpu_addr);

	return 0;

err_destroy_cq:
	efa_destroy_cq_idx(dev, cq->cq_idx);
err_free_mapped:
	dma_unmap_single(&dev->pdev->dev, cq->dma_addr, cq->size,
			 DMA_FROM_DEVICE);
	if (!cq_entry_inserted)
		free_pages_exact(cq->cpu_addr, cq->size);
err_out:
	atomic64_inc(&dev->stats.sw_stats.create_cq_err);
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

	rdma_for_each_block(umem->sg_head.sgl, &biter, umem->nmap,
			    BIT(hp_shift))
		page_list[hp_idx++] = rdma_block_iter_dma_address(&biter);

	return 0;
}

static struct scatterlist *efa_vmalloc_buf_to_sg(u64 *buf, int page_cnt)
{
	struct scatterlist *sglist;
	struct page *pg;
	int i;

	sglist = kcalloc(page_cnt, sizeof(*sglist), GFP_KERNEL);
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

struct ib_mr *efa_reg_mr(struct ib_pd *ibpd, u64 start, u64 length,
			 u64 virt_addr, int access_flags,
			 struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibpd->device);
	struct efa_com_reg_mr_params params = {};
	struct efa_com_reg_mr_result result = {};
	struct pbl_context pbl;
	unsigned int pg_sz;
	struct efa_mr *mr;
	int inline_size;
	int err;

	if (udata->inlen &&
	    !ib_is_udata_cleared(udata, 0, sizeof(udata->inlen))) {
		ibdev_dbg(&dev->ibdev,
			  "Incompatible ABI params, udata not cleared\n");
		err = -EINVAL;
		goto err_out;
	}

	if (access_flags & ~EFA_SUPPORTED_ACCESS_FLAGS) {
		ibdev_dbg(&dev->ibdev,
			  "Unsupported access flags[%#x], supported[%#x]\n",
			  access_flags, EFA_SUPPORTED_ACCESS_FLAGS);
		err = -EOPNOTSUPP;
		goto err_out;
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		err = -ENOMEM;
		goto err_out;
	}

	mr->umem = ib_umem_get(udata, start, length, access_flags, 0);
	if (IS_ERR(mr->umem)) {
		err = PTR_ERR(mr->umem);
		ibdev_dbg(&dev->ibdev,
			  "Failed to pin and map user space memory[%d]\n", err);
		goto err_free;
	}

	params.pd = to_epd(ibpd)->pdn;
	params.iova = virt_addr;
	params.mr_length_in_bytes = length;
	params.permissions = access_flags & 0x1;

	pg_sz = ib_umem_find_best_pgsz(mr->umem,
				       dev->dev_attr.page_size_cap,
				       virt_addr);
	if (!pg_sz) {
		err = -EOPNOTSUPP;
		ibdev_dbg(&dev->ibdev, "Failed to find a suitable page size in page_size_cap %#llx\n",
			  dev->dev_attr.page_size_cap);
		goto err_unmap;
	}

	params.page_shift = __ffs(pg_sz);
	params.page_num = DIV_ROUND_UP(length + (start & (pg_sz - 1)),
				       pg_sz);

	ibdev_dbg(&dev->ibdev,
		  "start %#llx length %#llx params.page_shift %u params.page_num %u\n",
		  start, length, params.page_shift, params.page_num);

	inline_size = ARRAY_SIZE(params.pbl.inline_pbl_array);
	if (params.page_num <= inline_size) {
		err = efa_create_inline_pbl(dev, mr, &params);
		if (err)
			goto err_unmap;

		err = efa_com_register_mr(&dev->edev, &params, &result);
		if (err)
			goto err_unmap;
	} else {
		err = efa_create_pbl(dev, &pbl, mr, &params);
		if (err)
			goto err_unmap;

		err = efa_com_register_mr(&dev->edev, &params, &result);
		pbl_destroy(dev, &pbl);

		if (err)
			goto err_unmap;
	}

	mr->ibmr.lkey = result.l_key;
	mr->ibmr.rkey = result.r_key;
	mr->ibmr.length = length;
	ibdev_dbg(&dev->ibdev, "Registered mr[%d]\n", mr->ibmr.lkey);

	return &mr->ibmr;

err_unmap:
	ib_umem_release(mr->umem);
err_free:
	kfree(mr);
err_out:
	atomic64_inc(&dev->stats.sw_stats.reg_mr_err);
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

int efa_get_port_immutable(struct ib_device *ibdev, u8 port_num,
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

int efa_alloc_ucontext(struct ib_ucontext *ibucontext, struct ib_udata *udata)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);
	struct efa_ibv_alloc_ucontext_resp resp = {};
	struct efa_com_alloc_uar_result result;
	int err;

	/*
	 * it's fine if the driver does not know all request fields,
	 * we will ack input fields in our response.
	 */

	err = efa_com_alloc_uar(&dev->edev, &result);
	if (err)
		goto err_out;

	ucontext->uarn = result.uarn;
	xa_init(&ucontext->mmap_xa);

	resp.cmds_supp_udata_mask |= EFA_USER_CMDS_SUPP_UDATA_QUERY_DEVICE;
	resp.cmds_supp_udata_mask |= EFA_USER_CMDS_SUPP_UDATA_CREATE_AH;
	resp.sub_cqs_per_cq = dev->dev_attr.sub_cqs_per_cq;
	resp.inline_buf_size = dev->dev_attr.inline_buf_size;
	resp.max_llq_size = dev->dev_attr.max_llq_size;

	if (udata && udata->outlen) {
		err = ib_copy_to_udata(udata, &resp,
				       min(sizeof(resp), udata->outlen));
		if (err)
			goto err_dealloc_uar;
	}

	return 0;

err_dealloc_uar:
	efa_dealloc_uar(dev, result.uarn);
err_out:
	atomic64_inc(&dev->stats.sw_stats.alloc_ucontext_err);
	return err;
}

void efa_dealloc_ucontext(struct ib_ucontext *ibucontext)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);

	mmap_entries_remove_free(dev, ucontext);
	efa_dealloc_uar(dev, ucontext->uarn);
}

static int __efa_mmap(struct efa_dev *dev, struct efa_ucontext *ucontext,
		      struct vm_area_struct *vma, u64 key, u64 length)
{
	struct efa_mmap_entry *entry;
	unsigned long va;
	u64 pfn;
	int err;

	entry = mmap_entry_get(dev, ucontext, key, length);
	if (!entry) {
		ibdev_dbg(&dev->ibdev, "key[%#llx] does not have valid entry\n",
			  key);
		return -EINVAL;
	}

	ibdev_dbg(&dev->ibdev,
		  "Mapping address[%#llx], length[%#llx], mmap_flag[%d]\n",
		  entry->address, length, entry->mmap_flag);

	pfn = entry->address >> PAGE_SHIFT;
	switch (entry->mmap_flag) {
	case EFA_MMAP_IO_NC:
		err = rdma_user_mmap_io(&ucontext->ibucontext, vma, pfn, length,
					pgprot_noncached(vma->vm_page_prot),
					NULL);
		break;
	case EFA_MMAP_IO_WC:
		err = rdma_user_mmap_io(&ucontext->ibucontext, vma, pfn, length,
					pgprot_writecombine(vma->vm_page_prot),
					NULL);
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
			"Couldn't mmap address[%#llx] length[%#llx] mmap_flag[%d] err[%d]\n",
			entry->address, length, entry->mmap_flag, err);
		return err;
	}

	return 0;
}

int efa_mmap(struct ib_ucontext *ibucontext,
	     struct vm_area_struct *vma)
{
	struct efa_ucontext *ucontext = to_eucontext(ibucontext);
	struct efa_dev *dev = to_edev(ibucontext->device);
	u64 length = vma->vm_end - vma->vm_start;
	u64 key = vma->vm_pgoff << PAGE_SHIFT;

	ibdev_dbg(&dev->ibdev,
		  "start %#lx, end %#lx, length = %#llx, key = %#llx\n",
		  vma->vm_start, vma->vm_end, length, key);

	if (length % PAGE_SIZE != 0 || !(vma->vm_flags & VM_SHARED)) {
		ibdev_dbg(&dev->ibdev,
			  "length[%#llx] is not page size aligned[%#lx] or VM_SHARED is not set [%#lx]\n",
			  length, PAGE_SIZE, vma->vm_flags);
		return -EINVAL;
	}

	if (vma->vm_flags & VM_EXEC) {
		ibdev_dbg(&dev->ibdev, "Mapping executable pages is not permitted\n");
		return -EPERM;
	}

	return __efa_mmap(dev, ucontext, vma, key, length);
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
		  struct rdma_ah_attr *ah_attr,
		  u32 flags,
		  struct ib_udata *udata)
{
	struct efa_dev *dev = to_edev(ibah->device);
	struct efa_com_create_ah_params params = {};
	struct efa_ibv_create_ah_resp resp = {};
	struct efa_com_create_ah_result result;
	struct efa_ah *ah = to_eah(ibah);
	int err;

	if (!(flags & RDMA_CREATE_AH_SLEEPABLE)) {
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
	atomic64_inc(&dev->stats.sw_stats.create_ah_err);
	return err;
}

void efa_destroy_ah(struct ib_ah *ibah, u32 flags)
{
	struct efa_dev *dev = to_edev(ibah->pd->device);
	struct efa_ah *ah = to_eah(ibah);

	ibdev_dbg(&dev->ibdev, "Destroy ah[%d]\n", ah->ah);

	if (!(flags & RDMA_DESTROY_AH_SLEEPABLE)) {
		ibdev_dbg(&dev->ibdev,
			  "Destroy address handle is not supported in atomic context\n");
		return;
	}

	efa_ah_destroy(dev, ah);
}

struct rdma_hw_stats *efa_alloc_hw_stats(struct ib_device *ibdev, u8 port_num)
{
	return rdma_alloc_hw_stats_struct(efa_stats_names,
					  ARRAY_SIZE(efa_stats_names),
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

int efa_get_hw_stats(struct ib_device *ibdev, struct rdma_hw_stats *stats,
		     u8 port_num, int index)
{
	struct efa_com_get_stats_params params = {};
	union efa_com_get_stats_result result;
	struct efa_dev *dev = to_edev(ibdev);
	struct efa_com_basic_stats *bs;
	struct efa_com_stats_admin *as;
	struct efa_stats *s;
	int err;

	params.type = EFA_ADMIN_GET_STATS_TYPE_BASIC;
	params.scope = EFA_ADMIN_GET_STATS_SCOPE_ALL;

	err = efa_com_get_stats(&dev->edev, &params, &result);
	if (err)
		return err;

	bs = &result.basic_stats;
	stats->value[EFA_TX_BYTES] = bs->tx_bytes;
	stats->value[EFA_TX_PKTS] = bs->tx_pkts;
	stats->value[EFA_RX_BYTES] = bs->rx_bytes;
	stats->value[EFA_RX_PKTS] = bs->rx_pkts;
	stats->value[EFA_RX_DROPS] = bs->rx_drops;

	as = &dev->edev.aq.stats;
	stats->value[EFA_SUBMITTED_CMDS] = atomic64_read(&as->submitted_cmd);
	stats->value[EFA_COMPLETED_CMDS] = atomic64_read(&as->completed_cmd);
	stats->value[EFA_NO_COMPLETION_CMDS] = atomic64_read(&as->no_completion);

	s = &dev->stats;
	stats->value[EFA_KEEP_ALIVE_RCVD] = atomic64_read(&s->keep_alive_rcvd);
	stats->value[EFA_ALLOC_PD_ERR] = atomic64_read(&s->sw_stats.alloc_pd_err);
	stats->value[EFA_CREATE_QP_ERR] = atomic64_read(&s->sw_stats.create_qp_err);
	stats->value[EFA_REG_MR_ERR] = atomic64_read(&s->sw_stats.reg_mr_err);
	stats->value[EFA_ALLOC_UCONTEXT_ERR] = atomic64_read(&s->sw_stats.alloc_ucontext_err);
	stats->value[EFA_CREATE_AH_ERR] = atomic64_read(&s->sw_stats.create_ah_err);

	return ARRAY_SIZE(efa_stats_names);
}

enum rdma_link_layer efa_port_link_layer(struct ib_device *ibdev,
					 u8 port_num)
{
	return IB_LINK_LAYER_UNSPECIFIED;
}

