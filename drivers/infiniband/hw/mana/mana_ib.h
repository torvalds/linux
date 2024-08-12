/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Microsoft Corporation. All rights reserved.
 */

#ifndef _MANA_IB_H_
#define _MANA_IB_H_

#include <rdma/ib_verbs.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_umem.h>
#include <rdma/mana-abi.h>
#include <rdma/uverbs_ioctl.h>

#include <net/mana/mana.h>

#define PAGE_SZ_BM                                                             \
	(SZ_4K | SZ_8K | SZ_16K | SZ_32K | SZ_64K | SZ_128K | SZ_256K |        \
	 SZ_512K | SZ_1M | SZ_2M)

/* MANA doesn't have any limit for MR size */
#define MANA_IB_MAX_MR_SIZE	U64_MAX

/*
 * The hardware limit of number of MRs is greater than maximum number of MRs
 * that can possibly represent in 24 bits
 */
#define MANA_IB_MAX_MR		0xFFFFFFu

/*
 * The CA timeout is approx. 260ms (4us * 2^(DELAY))
 */
#define MANA_CA_ACK_DELAY	16

struct mana_ib_adapter_caps {
	u32 max_sq_id;
	u32 max_rq_id;
	u32 max_cq_id;
	u32 max_qp_count;
	u32 max_cq_count;
	u32 max_mr_count;
	u32 max_pd_count;
	u32 max_inbound_read_limit;
	u32 max_outbound_read_limit;
	u32 mw_count;
	u32 max_srq_count;
	u32 max_qp_wr;
	u32 max_send_sge_count;
	u32 max_recv_sge_count;
	u32 max_inline_data_size;
};

struct mana_ib_queue {
	struct ib_umem *umem;
	u64 gdma_region;
	u64 id;
};

struct mana_ib_dev {
	struct ib_device ib_dev;
	struct gdma_dev *gdma_dev;
	mana_handle_t adapter_handle;
	struct gdma_queue *fatal_err_eq;
	struct gdma_queue **eqs;
	struct xarray qp_table_wq;
	struct mana_ib_adapter_caps adapter_caps;
};

struct mana_ib_wq {
	struct ib_wq ibwq;
	struct mana_ib_queue queue;
	int wqe;
	u32 wq_buf_size;
	mana_handle_t rx_object;
};

struct mana_ib_pd {
	struct ib_pd ibpd;
	u32 pdn;
	mana_handle_t pd_handle;

	/* Mutex for sharing access to vport_use_count */
	struct mutex vport_mutex;
	int vport_use_count;

	bool tx_shortform_allowed;
	u32 tx_vp_offset;
};

struct mana_ib_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
	mana_handle_t mr_handle;
};

struct mana_ib_cq {
	struct ib_cq ibcq;
	struct mana_ib_queue queue;
	int cqe;
	u32 comp_vector;
	mana_handle_t  cq_handle;
};

enum mana_rc_queue_type {
	MANA_RC_SEND_QUEUE_REQUESTER = 0,
	MANA_RC_SEND_QUEUE_RESPONDER,
	MANA_RC_SEND_QUEUE_FMR,
	MANA_RC_RECV_QUEUE_REQUESTER,
	MANA_RC_RECV_QUEUE_RESPONDER,
	MANA_RC_QUEUE_TYPE_MAX,
};

struct mana_ib_rc_qp {
	struct mana_ib_queue queues[MANA_RC_QUEUE_TYPE_MAX];
};

struct mana_ib_qp {
	struct ib_qp ibqp;

	mana_handle_t qp_handle;
	union {
		struct mana_ib_queue raw_sq;
		struct mana_ib_rc_qp rc_qp;
	};

	/* The port on the IB device, starting with 1 */
	u32 port;

	refcount_t		refcount;
	struct completion	free;
};

struct mana_ib_ucontext {
	struct ib_ucontext ibucontext;
	u32 doorbell;
};

struct mana_ib_rwq_ind_table {
	struct ib_rwq_ind_table ib_ind_table;
};

enum mana_ib_command_code {
	MANA_IB_GET_ADAPTER_CAP = 0x30001,
	MANA_IB_CREATE_ADAPTER  = 0x30002,
	MANA_IB_DESTROY_ADAPTER = 0x30003,
	MANA_IB_CONFIG_IP_ADDR	= 0x30004,
	MANA_IB_CONFIG_MAC_ADDR	= 0x30005,
	MANA_IB_CREATE_CQ       = 0x30008,
	MANA_IB_DESTROY_CQ      = 0x30009,
	MANA_IB_CREATE_RC_QP    = 0x3000a,
	MANA_IB_DESTROY_RC_QP   = 0x3000b,
	MANA_IB_SET_QP_STATE	= 0x3000d,
};

struct mana_ib_query_adapter_caps_req {
	struct gdma_req_hdr hdr;
}; /*HW Data */

struct mana_ib_query_adapter_caps_resp {
	struct gdma_resp_hdr hdr;
	u32 max_sq_id;
	u32 max_rq_id;
	u32 max_cq_id;
	u32 max_qp_count;
	u32 max_cq_count;
	u32 max_mr_count;
	u32 max_pd_count;
	u32 max_inbound_read_limit;
	u32 max_outbound_read_limit;
	u32 mw_count;
	u32 max_srq_count;
	u32 max_requester_sq_size;
	u32 max_responder_sq_size;
	u32 max_requester_rq_size;
	u32 max_responder_rq_size;
	u32 max_send_sge_count;
	u32 max_recv_sge_count;
	u32 max_inline_data_size;
}; /* HW Data */

struct mana_rnic_create_adapter_req {
	struct gdma_req_hdr hdr;
	u32 notify_eq_id;
	u32 reserved;
	u64 feature_flags;
}; /*HW Data */

struct mana_rnic_create_adapter_resp {
	struct gdma_resp_hdr hdr;
	mana_handle_t adapter;
}; /* HW Data */

struct mana_rnic_destroy_adapter_req {
	struct gdma_req_hdr hdr;
	mana_handle_t adapter;
}; /*HW Data */

struct mana_rnic_destroy_adapter_resp {
	struct gdma_resp_hdr hdr;
}; /* HW Data */

enum mana_ib_addr_op {
	ADDR_OP_ADD = 1,
	ADDR_OP_REMOVE = 2,
};

enum sgid_entry_type {
	SGID_TYPE_IPV4 = 1,
	SGID_TYPE_IPV6 = 2,
};

struct mana_rnic_config_addr_req {
	struct gdma_req_hdr hdr;
	mana_handle_t adapter;
	enum mana_ib_addr_op op;
	enum sgid_entry_type sgid_type;
	u8 ip_addr[16];
}; /* HW Data */

struct mana_rnic_config_addr_resp {
	struct gdma_resp_hdr hdr;
}; /* HW Data */

struct mana_rnic_config_mac_addr_req {
	struct gdma_req_hdr hdr;
	mana_handle_t adapter;
	enum mana_ib_addr_op op;
	u8 mac_addr[ETH_ALEN];
	u8 reserved[6];
}; /* HW Data */

struct mana_rnic_config_mac_addr_resp {
	struct gdma_resp_hdr hdr;
}; /* HW Data */

struct mana_rnic_create_cq_req {
	struct gdma_req_hdr hdr;
	mana_handle_t adapter;
	u64 gdma_region;
	u32 eq_id;
	u32 doorbell_page;
}; /* HW Data */

struct mana_rnic_create_cq_resp {
	struct gdma_resp_hdr hdr;
	mana_handle_t cq_handle;
	u32 cq_id;
	u32 reserved;
}; /* HW Data */

struct mana_rnic_destroy_cq_req {
	struct gdma_req_hdr hdr;
	mana_handle_t adapter;
	mana_handle_t cq_handle;
}; /* HW Data */

struct mana_rnic_destroy_cq_resp {
	struct gdma_resp_hdr hdr;
}; /* HW Data */

enum mana_rnic_create_rc_flags {
	MANA_RC_FLAG_NO_FMR = 2,
};

struct mana_rnic_create_qp_req {
	struct gdma_req_hdr hdr;
	mana_handle_t adapter;
	mana_handle_t pd_handle;
	mana_handle_t send_cq_handle;
	mana_handle_t recv_cq_handle;
	u64 dma_region[MANA_RC_QUEUE_TYPE_MAX];
	u64 deprecated[2];
	u64 flags;
	u32 doorbell_page;
	u32 max_send_wr;
	u32 max_recv_wr;
	u32 max_send_sge;
	u32 max_recv_sge;
	u32 reserved;
}; /* HW Data */

struct mana_rnic_create_qp_resp {
	struct gdma_resp_hdr hdr;
	mana_handle_t rc_qp_handle;
	u32 queue_ids[MANA_RC_QUEUE_TYPE_MAX];
	u32 reserved;
}; /* HW Data*/

struct mana_rnic_destroy_rc_qp_req {
	struct gdma_req_hdr hdr;
	mana_handle_t adapter;
	mana_handle_t rc_qp_handle;
}; /* HW Data */

struct mana_rnic_destroy_rc_qp_resp {
	struct gdma_resp_hdr hdr;
}; /* HW Data */

struct mana_ib_ah_attr {
	u8 src_addr[16];
	u8 dest_addr[16];
	u8 src_mac[ETH_ALEN];
	u8 dest_mac[ETH_ALEN];
	u8 src_addr_type;
	u8 dest_addr_type;
	u8 hop_limit;
	u8 traffic_class;
	u16 src_port;
	u16 dest_port;
	u32 reserved;
};

struct mana_rnic_set_qp_state_req {
	struct gdma_req_hdr hdr;
	mana_handle_t adapter;
	mana_handle_t qp_handle;
	u64 attr_mask;
	u32 qp_state;
	u32 path_mtu;
	u32 rq_psn;
	u32 sq_psn;
	u32 dest_qpn;
	u32 max_dest_rd_atomic;
	u32 retry_cnt;
	u32 rnr_retry;
	u32 min_rnr_timer;
	u32 reserved;
	struct mana_ib_ah_attr ah_attr;
}; /* HW Data */

struct mana_rnic_set_qp_state_resp {
	struct gdma_resp_hdr hdr;
}; /* HW Data */

static inline struct gdma_context *mdev_to_gc(struct mana_ib_dev *mdev)
{
	return mdev->gdma_dev->gdma_context;
}

static inline struct mana_ib_qp *mana_get_qp_ref(struct mana_ib_dev *mdev,
						 uint32_t qid)
{
	struct mana_ib_qp *qp;
	unsigned long flag;

	xa_lock_irqsave(&mdev->qp_table_wq, flag);
	qp = xa_load(&mdev->qp_table_wq, qid);
	if (qp)
		refcount_inc(&qp->refcount);
	xa_unlock_irqrestore(&mdev->qp_table_wq, flag);
	return qp;
}

static inline void mana_put_qp_ref(struct mana_ib_qp *qp)
{
	if (refcount_dec_and_test(&qp->refcount))
		complete(&qp->free);
}

static inline struct net_device *mana_ib_get_netdev(struct ib_device *ibdev, u32 port)
{
	struct mana_ib_dev *mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct mana_context *mc = gc->mana.driver_data;

	if (port < 1 || port > mc->num_ports)
		return NULL;
	return mc->ports[port - 1];
}

static inline void copy_in_reverse(u8 *dst, const u8 *src, u32 size)
{
	u32 i;

	for (i = 0; i < size; i++)
		dst[size - 1 - i] = src[i];
}

int mana_ib_install_cq_cb(struct mana_ib_dev *mdev, struct mana_ib_cq *cq);
void mana_ib_remove_cq_cb(struct mana_ib_dev *mdev, struct mana_ib_cq *cq);

int mana_ib_create_zero_offset_dma_region(struct mana_ib_dev *dev, struct ib_umem *umem,
					  mana_handle_t *gdma_region);

int mana_ib_create_dma_region(struct mana_ib_dev *dev, struct ib_umem *umem,
			      mana_handle_t *gdma_region, u64 virt);

int mana_ib_gd_destroy_dma_region(struct mana_ib_dev *dev,
				  mana_handle_t gdma_region);

int mana_ib_create_queue(struct mana_ib_dev *mdev, u64 addr, u32 size,
			 struct mana_ib_queue *queue);
void mana_ib_destroy_queue(struct mana_ib_dev *mdev, struct mana_ib_queue *queue);

struct ib_wq *mana_ib_create_wq(struct ib_pd *pd,
				struct ib_wq_init_attr *init_attr,
				struct ib_udata *udata);

int mana_ib_modify_wq(struct ib_wq *wq, struct ib_wq_attr *wq_attr,
		      u32 wq_attr_mask, struct ib_udata *udata);

int mana_ib_destroy_wq(struct ib_wq *ibwq, struct ib_udata *udata);

int mana_ib_create_rwq_ind_table(struct ib_rwq_ind_table *ib_rwq_ind_table,
				 struct ib_rwq_ind_table_init_attr *init_attr,
				 struct ib_udata *udata);

int mana_ib_destroy_rwq_ind_table(struct ib_rwq_ind_table *ib_rwq_ind_tbl);

struct ib_mr *mana_ib_get_dma_mr(struct ib_pd *ibpd, int access_flags);

struct ib_mr *mana_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 iova, int access_flags,
				  struct ib_udata *udata);

int mana_ib_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata);

int mana_ib_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *qp_init_attr,
		      struct ib_udata *udata);

int mana_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata);

int mana_ib_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata);

int mana_ib_cfg_vport(struct mana_ib_dev *dev, u32 port_id,
		      struct mana_ib_pd *pd, u32 doorbell_id);
void mana_ib_uncfg_vport(struct mana_ib_dev *dev, struct mana_ib_pd *pd,
			 u32 port);

int mana_ib_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		      struct uverbs_attr_bundle *attrs);

int mana_ib_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);

int mana_ib_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
int mana_ib_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);

int mana_ib_alloc_ucontext(struct ib_ucontext *ibcontext,
			   struct ib_udata *udata);
void mana_ib_dealloc_ucontext(struct ib_ucontext *ibcontext);

int mana_ib_mmap(struct ib_ucontext *ibcontext, struct vm_area_struct *vma);

int mana_ib_get_port_immutable(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_immutable *immutable);
int mana_ib_query_device(struct ib_device *ibdev, struct ib_device_attr *props,
			 struct ib_udata *uhw);
int mana_ib_query_port(struct ib_device *ibdev, u32 port,
		       struct ib_port_attr *props);
int mana_ib_query_gid(struct ib_device *ibdev, u32 port, int index,
		      union ib_gid *gid);

void mana_ib_disassociate_ucontext(struct ib_ucontext *ibcontext);

int mana_ib_gd_query_adapter_caps(struct mana_ib_dev *mdev);

int mana_ib_create_eqs(struct mana_ib_dev *mdev);

void mana_ib_destroy_eqs(struct mana_ib_dev *mdev);

int mana_ib_gd_create_rnic_adapter(struct mana_ib_dev *mdev);

int mana_ib_gd_destroy_rnic_adapter(struct mana_ib_dev *mdev);

int mana_ib_query_pkey(struct ib_device *ibdev, u32 port, u16 index, u16 *pkey);

enum rdma_link_layer mana_ib_get_link_layer(struct ib_device *device, u32 port_num);

int mana_ib_gd_add_gid(const struct ib_gid_attr *attr, void **context);

int mana_ib_gd_del_gid(const struct ib_gid_attr *attr, void **context);

int mana_ib_gd_config_mac(struct mana_ib_dev *mdev, enum mana_ib_addr_op op, u8 *mac);

int mana_ib_gd_create_cq(struct mana_ib_dev *mdev, struct mana_ib_cq *cq, u32 doorbell);

int mana_ib_gd_destroy_cq(struct mana_ib_dev *mdev, struct mana_ib_cq *cq);

int mana_ib_gd_create_rc_qp(struct mana_ib_dev *mdev, struct mana_ib_qp *qp,
			    struct ib_qp_init_attr *attr, u32 doorbell, u64 flags);
int mana_ib_gd_destroy_rc_qp(struct mana_ib_dev *mdev, struct mana_ib_qp *qp);
#endif
