/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2015 - 2021 Intel Corporation */
#ifndef IRDMA_VERBS_H
#define IRDMA_VERBS_H

#define IRDMA_MAX_SAVED_PHY_PGADDR	4
#define IRDMA_FLUSH_DELAY_MS		20

#define IRDMA_PKEY_TBL_SZ		1
#define IRDMA_DEFAULT_PKEY		0xFFFF

struct irdma_ucontext {
	struct ib_ucontext ibucontext;
	struct irdma_device *iwdev;
	struct rdma_user_mmap_entry *db_mmap_entry;
	struct list_head cq_reg_mem_list;
	spinlock_t cq_reg_mem_list_lock; /* protect CQ memory list */
	struct list_head qp_reg_mem_list;
	spinlock_t qp_reg_mem_list_lock; /* protect QP memory list */
	int abi_ver;
	u8 legacy_mode : 1;
	u8 use_raw_attrs : 1;
};

struct irdma_pd {
	struct ib_pd ibpd;
	struct irdma_sc_pd sc_pd;
};

union irdma_sockaddr {
	struct sockaddr_in saddr_in;
	struct sockaddr_in6 saddr_in6;
};

struct irdma_av {
	u8 macaddr[16];
	struct rdma_ah_attr attrs;
	union irdma_sockaddr sgid_addr;
	union irdma_sockaddr dgid_addr;
	u8 net_type;
};

struct irdma_ah {
	struct ib_ah ibah;
	struct irdma_sc_ah sc_ah;
	struct irdma_pd *pd;
	struct irdma_av av;
	u8 sgid_index;
	union ib_gid dgid;
	struct hlist_node list;
	refcount_t refcnt;
	struct irdma_ah *parent_ah; /* AH from cached list */
};

struct irdma_hmc_pble {
	union {
		u32 idx;
		dma_addr_t addr;
	};
};

struct irdma_cq_mr {
	struct irdma_hmc_pble cq_pbl;
	dma_addr_t shadow;
	bool split;
};

struct irdma_qp_mr {
	struct irdma_hmc_pble sq_pbl;
	struct irdma_hmc_pble rq_pbl;
	dma_addr_t shadow;
	struct page *sq_page;
};

struct irdma_cq_buf {
	struct irdma_dma_mem kmem_buf;
	struct irdma_cq_uk cq_uk;
	struct irdma_hw *hw;
	struct list_head list;
	struct work_struct work;
};

struct irdma_pbl {
	struct list_head list;
	union {
		struct irdma_qp_mr qp_mr;
		struct irdma_cq_mr cq_mr;
	};

	bool pbl_allocated:1;
	bool on_list:1;
	u64 user_base;
	struct irdma_pble_alloc pble_alloc;
	struct irdma_mr *iwmr;
};

struct irdma_mr {
	union {
		struct ib_mr ibmr;
		struct ib_mw ibmw;
	};
	struct ib_umem *region;
	int access;
	u8 is_hwreg;
	u16 type;
	u32 page_cnt;
	u64 page_size;
	u32 npages;
	u32 stag;
	u64 len;
	u64 pgaddrmem[IRDMA_MAX_SAVED_PHY_PGADDR];
	struct irdma_pbl iwpbl;
};

struct irdma_cq {
	struct ib_cq ibcq;
	struct irdma_sc_cq sc_cq;
	u16 cq_head;
	u16 cq_size;
	u16 cq_num;
	bool user_mode;
	atomic_t armed;
	enum irdma_cmpl_notify last_notify;
	u32 polled_cmpls;
	u32 cq_mem_size;
	struct irdma_dma_mem kmem;
	struct irdma_dma_mem kmem_shadow;
	struct completion free_cq;
	refcount_t refcnt;
	spinlock_t lock; /* for poll cq */
	struct irdma_pbl *iwpbl;
	struct irdma_pbl *iwpbl_shadow;
	struct list_head resize_list;
	struct irdma_cq_poll_info cur_cqe;
	struct list_head cmpl_generated;
};

struct irdma_cmpl_gen {
	struct list_head list;
	struct irdma_cq_poll_info cpi;
};

struct disconn_work {
	struct work_struct work;
	struct irdma_qp *iwqp;
};

struct iw_cm_id;

struct irdma_qp_kmode {
	struct irdma_dma_mem dma_mem;
	struct irdma_sq_uk_wr_trk_info *sq_wrid_mem;
	u64 *rq_wrid_mem;
};

struct irdma_qp {
	struct ib_qp ibqp;
	struct irdma_sc_qp sc_qp;
	struct irdma_device *iwdev;
	struct irdma_cq *iwscq;
	struct irdma_cq *iwrcq;
	struct irdma_pd *iwpd;
	struct rdma_user_mmap_entry *push_wqe_mmap_entry;
	struct rdma_user_mmap_entry *push_db_mmap_entry;
	struct irdma_qp_host_ctx_info ctx_info;
	union {
		struct irdma_iwarp_offload_info iwarp_info;
		struct irdma_roce_offload_info roce_info;
	};

	union {
		struct irdma_tcp_offload_info tcp_info;
		struct irdma_udp_offload_info udp_info;
	};

	struct irdma_ah roce_ah;
	struct list_head teardown_entry;
	refcount_t refcnt;
	struct iw_cm_id *cm_id;
	struct irdma_cm_node *cm_node;
	struct delayed_work dwork_flush;
	struct ib_mr *lsmm_mr;
	atomic_t hw_mod_qp_pend;
	enum ib_qp_state ibqp_state;
	u32 qp_mem_size;
	u32 last_aeq;
	int max_send_wr;
	int max_recv_wr;
	atomic_t close_timer_started;
	spinlock_t lock; /* serialize posting WRs to SQ/RQ */
	struct irdma_qp_context *iwqp_context;
	void *pbl_vbase;
	dma_addr_t pbl_pbase;
	struct page *page;
	u8 active_conn : 1;
	u8 user_mode : 1;
	u8 hte_added : 1;
	u8 flush_issued : 1;
	u8 sig_all : 1;
	u8 pau_mode : 1;
	u8 rsvd : 1;
	u8 iwarp_state;
	u16 term_sq_flush_code;
	u16 term_rq_flush_code;
	u8 hw_iwarp_state;
	u8 hw_tcp_state;
	struct irdma_qp_kmode kqp;
	struct irdma_dma_mem host_ctx;
	struct timer_list terminate_timer;
	struct irdma_pbl *iwpbl;
	struct irdma_dma_mem q2_ctx_mem;
	struct irdma_dma_mem ietf_mem;
	struct completion free_qp;
	wait_queue_head_t waitq;
	wait_queue_head_t mod_qp_waitq;
	u8 rts_ae_rcvd;
};

enum irdma_mmap_flag {
	IRDMA_MMAP_IO_NC,
	IRDMA_MMAP_IO_WC,
};

struct irdma_user_mmap_entry {
	struct rdma_user_mmap_entry rdma_entry;
	u64 bar_offset;
	u8 mmap_flag;
};

static inline u16 irdma_fw_major_ver(struct irdma_sc_dev *dev)
{
	return (u16)FIELD_GET(IRDMA_FW_VER_MAJOR, dev->feature_info[IRDMA_FEATURE_FW_INFO]);
}

static inline u16 irdma_fw_minor_ver(struct irdma_sc_dev *dev)
{
	return (u16)FIELD_GET(IRDMA_FW_VER_MINOR, dev->feature_info[IRDMA_FEATURE_FW_INFO]);
}

static inline void set_ib_wc_op_sq(struct irdma_cq_poll_info *cq_poll_info,
				   struct ib_wc *entry)
{
	switch (cq_poll_info->op_type) {
	case IRDMA_OP_TYPE_RDMA_WRITE:
	case IRDMA_OP_TYPE_RDMA_WRITE_SOL:
		entry->opcode = IB_WC_RDMA_WRITE;
		break;
	case IRDMA_OP_TYPE_RDMA_READ_INV_STAG:
	case IRDMA_OP_TYPE_RDMA_READ:
		entry->opcode = IB_WC_RDMA_READ;
		break;
	case IRDMA_OP_TYPE_SEND_SOL:
	case IRDMA_OP_TYPE_SEND_SOL_INV:
	case IRDMA_OP_TYPE_SEND_INV:
	case IRDMA_OP_TYPE_SEND:
		entry->opcode = IB_WC_SEND;
		break;
	case IRDMA_OP_TYPE_FAST_REG_NSMR:
		entry->opcode = IB_WC_REG_MR;
		break;
	case IRDMA_OP_TYPE_INV_STAG:
		entry->opcode = IB_WC_LOCAL_INV;
		break;
	default:
		entry->status = IB_WC_GENERAL_ERR;
	}
}

static inline void set_ib_wc_op_rq(struct irdma_cq_poll_info *cq_poll_info,
				   struct ib_wc *entry, bool send_imm_support)
{
	/**
	 * iWARP does not support sendImm, so the presence of Imm data
	 * must be WriteImm.
	 */
	if (!send_imm_support) {
		entry->opcode = cq_poll_info->imm_valid ?
					IB_WC_RECV_RDMA_WITH_IMM :
					IB_WC_RECV;
		return;
	}

	switch (cq_poll_info->op_type) {
	case IB_OPCODE_RDMA_WRITE_ONLY_WITH_IMMEDIATE:
	case IB_OPCODE_RDMA_WRITE_LAST_WITH_IMMEDIATE:
		entry->opcode = IB_WC_RECV_RDMA_WITH_IMM;
		break;
	default:
		entry->opcode = IB_WC_RECV;
	}
}

void irdma_mcast_mac(u32 *ip_addr, u8 *mac, bool ipv4);
int irdma_ib_register_device(struct irdma_device *iwdev);
void irdma_ib_unregister_device(struct irdma_device *iwdev);
void irdma_ib_dealloc_device(struct ib_device *ibdev);
void irdma_ib_qp_event(struct irdma_qp *iwqp, enum irdma_qp_event_type event);
void irdma_generate_flush_completions(struct irdma_qp *iwqp);
void irdma_remove_cmpls_list(struct irdma_cq *iwcq);
int irdma_generated_cmpls(struct irdma_cq *iwcq, struct irdma_cq_poll_info *cq_poll_info);
#endif /* IRDMA_VERBS_H */
