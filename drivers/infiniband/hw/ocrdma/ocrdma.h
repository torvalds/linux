/* This file is part of the Emulex RoCE Device Driver for
 * RoCE (RDMA over Converged Ethernet) adapters.
 * Copyright (C) 2012-2015 Emulex. All rights reserved.
 * EMULEX and SLI are trademarks of Emulex.
 * www.emulex.com
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#ifndef __OCRDMA_H__
#define __OCRDMA_H__

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/pci.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_addr.h>

#include <be_roce.h>
#include "ocrdma_sli.h"

#define OCRDMA_ROCE_DRV_VERSION "10.6.0.0"

#define OCRDMA_ROCE_DRV_DESC "Emulex OneConnect RoCE Driver"
#define OCRDMA_NODE_DESC "Emulex OneConnect RoCE HCA"

#define OC_NAME_SH	OCRDMA_NODE_DESC "(Skyhawk)"
#define OC_NAME_UNKNOWN OCRDMA_NODE_DESC "(Unknown)"

#define OC_SKH_DEVICE_PF 0x720
#define OC_SKH_DEVICE_VF 0x728
#define OCRDMA_MAX_AH 512

#define OCRDMA_UVERBS(CMD_NAME) (1ull << IB_USER_VERBS_CMD_##CMD_NAME)

#define convert_to_64bit(lo, hi) ((u64)hi << 32 | (u64)lo)
#define EQ_INTR_PER_SEC_THRSH_HI 150000
#define EQ_INTR_PER_SEC_THRSH_LOW 100000
#define EQ_AIC_MAX_EQD 20
#define EQ_AIC_MIN_EQD 0

void ocrdma_eqd_set_task(struct work_struct *work);

struct ocrdma_dev_attr {
	u8 fw_ver[32];
	u32 vendor_id;
	u32 device_id;
	u16 max_pd;
	u16 max_dpp_pds;
	u16 max_cq;
	u16 max_cqe;
	u16 max_qp;
	u16 max_wqe;
	u16 max_rqe;
	u16 max_srq;
	u32 max_inline_data;
	int max_send_sge;
	int max_recv_sge;
	int max_srq_sge;
	int max_rdma_sge;
	int max_mr;
	u64 max_mr_size;
	u32 max_num_mr_pbl;
	int max_mw;
	int max_fmr;
	int max_map_per_fmr;
	int max_pages_per_frmr;
	u16 max_ord_per_qp;
	u16 max_ird_per_qp;

	int device_cap_flags;
	u8 cq_overflow_detect;
	u8 srq_supported;

	u32 wqe_size;
	u32 rqe_size;
	u32 ird_page_size;
	u8 local_ca_ack_delay;
	u8 ird;
	u8 num_ird_pages;
};

struct ocrdma_dma_mem {
	void *va;
	dma_addr_t pa;
	u32 size;
};

struct ocrdma_pbl {
	void *va;
	dma_addr_t pa;
};

struct ocrdma_queue_info {
	void *va;
	dma_addr_t dma;
	u32 size;
	u16 len;
	u16 entry_size;		/* Size of an element in the queue */
	u16 id;			/* qid, where to ring the doorbell. */
	u16 head, tail;
	bool created;
};

struct ocrdma_aic_obj {         /* Adaptive interrupt coalescing (AIC) info */
	u32 prev_eqd;
	u64 eq_intr_cnt;
	u64 prev_eq_intr_cnt;
};

struct ocrdma_eq {
	struct ocrdma_queue_info q;
	u32 vector;
	int cq_cnt;
	struct ocrdma_dev *dev;
	char irq_name[32];
	struct ocrdma_aic_obj aic_obj;
};

struct ocrdma_mq {
	struct ocrdma_queue_info sq;
	struct ocrdma_queue_info cq;
	bool rearm_cq;
};

struct mqe_ctx {
	struct mutex lock; /* for serializing mailbox commands on MQ */
	wait_queue_head_t cmd_wait;
	u32 tag;
	u16 cqe_status;
	u16 ext_status;
	bool cmd_done;
	bool fw_error_state;
};

struct ocrdma_hw_mr {
	u32 lkey;
	u8 fr_mr;
	u8 remote_atomic;
	u8 remote_rd;
	u8 remote_wr;
	u8 local_rd;
	u8 local_wr;
	u8 mw_bind;
	u8 rsvd;
	u64 len;
	struct ocrdma_pbl *pbl_table;
	u32 num_pbls;
	u32 num_pbes;
	u32 pbl_size;
	u32 pbe_size;
	u64 fbo;
	u64 va;
};

struct ocrdma_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
	struct ocrdma_hw_mr hwmr;
};

struct ocrdma_stats {
	u8 type;
	struct ocrdma_dev *dev;
};

struct ocrdma_pd_resource_mgr {
	u32 pd_norm_start;
	u16 pd_norm_count;
	u16 pd_norm_thrsh;
	u16 max_normal_pd;
	u32 pd_dpp_start;
	u16 pd_dpp_count;
	u16 pd_dpp_thrsh;
	u16 max_dpp_pd;
	u16 dpp_page_index;
	unsigned long *pd_norm_bitmap;
	unsigned long *pd_dpp_bitmap;
	bool pd_prealloc_valid;
};

struct stats_mem {
	struct ocrdma_mqe mqe;
	void *va;
	dma_addr_t pa;
	u32 size;
	char *debugfs_mem;
};

struct phy_info {
	u16 auto_speeds_supported;
	u16 fixed_speeds_supported;
	u16 phy_type;
	u16 interface_type;
};

struct ocrdma_dev {
	struct ib_device ibdev;
	struct ocrdma_dev_attr attr;

	struct mutex dev_lock; /* provides syncronise access to device data */
	spinlock_t flush_q_lock ____cacheline_aligned;

	struct ocrdma_cq **cq_tbl;
	struct ocrdma_qp **qp_tbl;

	struct ocrdma_eq *eq_tbl;
	int eq_cnt;
	struct delayed_work eqd_work;
	u16 base_eqid;
	u16 max_eq;

	union ib_gid *sgid_tbl;
	/* provided synchronization to sgid table for
	 * updating gid entries triggered by notifier.
	 */
	spinlock_t sgid_lock;

	int gsi_qp_created;
	struct ocrdma_cq *gsi_sqcq;
	struct ocrdma_cq *gsi_rqcq;

	struct {
		struct ocrdma_av *va;
		dma_addr_t pa;
		u32 size;
		u32 num_ah;
		/* provide synchronization for av
		 * entry allocations.
		 */
		spinlock_t lock;
		u32 ahid;
		struct ocrdma_pbl pbl;
	} av_tbl;

	void *mbx_cmd;
	struct ocrdma_mq mq;
	struct mqe_ctx mqe_ctx;

	struct be_dev_info nic_info;
	struct phy_info phy;
	char model_number[32];
	u32 hba_port_num;

	struct list_head entry;
	struct rcu_head rcu;
	int id;
	u64 *stag_arr;
	u8 sl; /* service level */
	bool pfc_state;
	atomic_t update_sl;
	u16 pvid;
	u32 asic_id;

	ulong last_stats_time;
	struct mutex stats_lock; /* provide synch for debugfs operations */
	struct stats_mem stats_mem;
	struct ocrdma_stats rsrc_stats;
	struct ocrdma_stats rx_stats;
	struct ocrdma_stats wqe_stats;
	struct ocrdma_stats tx_stats;
	struct ocrdma_stats db_err_stats;
	struct ocrdma_stats tx_qp_err_stats;
	struct ocrdma_stats rx_qp_err_stats;
	struct ocrdma_stats tx_dbg_stats;
	struct ocrdma_stats rx_dbg_stats;
	struct ocrdma_stats driver_stats;
	struct ocrdma_stats reset_stats;
	struct dentry *dir;
	atomic_t async_err_stats[OCRDMA_MAX_ASYNC_ERRORS];
	atomic_t cqe_err_stats[OCRDMA_MAX_CQE_ERR];
	struct ocrdma_pd_resource_mgr *pd_mgr;
};

struct ocrdma_cq {
	struct ib_cq ibcq;
	struct ocrdma_cqe *va;
	u32 phase;
	u32 getp;	/* pointer to pending wrs to
			 * return to stack, wrap arounds
			 * at max_hw_cqe
			 */
	u32 max_hw_cqe;
	bool phase_change;
	bool deferred_arm, deferred_sol;
	bool first_arm;

	spinlock_t cq_lock ____cacheline_aligned; /* provide synchronization
						   * to cq polling
						   */
	/* syncronizes cq completion handler invoked from multiple context */
	spinlock_t comp_handler_lock ____cacheline_aligned;
	u16 id;
	u16 eqn;

	struct ocrdma_ucontext *ucontext;
	dma_addr_t pa;
	u32 len;
	u32 cqe_cnt;

	/* head of all qp's sq and rq for which cqes need to be flushed
	 * by the software.
	 */
	struct list_head sq_head, rq_head;
};

struct ocrdma_pd {
	struct ib_pd ibpd;
	struct ocrdma_ucontext *uctx;
	u32 id;
	int num_dpp_qp;
	u32 dpp_page;
	bool dpp_enabled;
};

struct ocrdma_ah {
	struct ib_ah ibah;
	struct ocrdma_av *av;
	u16 sgid_index;
	u32 id;
};

struct ocrdma_qp_hwq_info {
	u8 *va;			/* virtual address */
	u32 max_sges;
	u32 head, tail;
	u32 entry_size;
	u32 max_cnt;
	u32 max_wqe_idx;
	u16 dbid;		/* qid, where to ring the doorbell. */
	u32 len;
	dma_addr_t pa;
};

struct ocrdma_srq {
	struct ib_srq ibsrq;
	u8 __iomem *db;
	struct ocrdma_qp_hwq_info rq;
	u64 *rqe_wr_id_tbl;
	u32 *idx_bit_fields;
	u32 bit_fields_len;

	/* provide synchronization to multiple context(s) posting rqe */
	spinlock_t q_lock ____cacheline_aligned;

	struct ocrdma_pd *pd;
	u32 id;
};

struct ocrdma_qp {
	struct ib_qp ibqp;

	u8 __iomem *sq_db;
	struct ocrdma_qp_hwq_info sq;
	struct {
		uint64_t wrid;
		uint16_t dpp_wqe_idx;
		uint16_t dpp_wqe;
		uint8_t  signaled;
		uint8_t  rsvd[3];
	} *wqe_wr_id_tbl;
	u32 max_inline_data;

	/* provide synchronization to multiple context(s) posting wqe, rqe */
	spinlock_t q_lock ____cacheline_aligned;
	struct ocrdma_cq *sq_cq;
	/* list maintained per CQ to flush SQ errors */
	struct list_head sq_entry;

	u8 __iomem *rq_db;
	struct ocrdma_qp_hwq_info rq;
	u64 *rqe_wr_id_tbl;
	struct ocrdma_cq *rq_cq;
	struct ocrdma_srq *srq;
	/* list maintained per CQ to flush RQ errors */
	struct list_head rq_entry;

	enum ocrdma_qp_state state;	/*  QP state */
	int cap_flags;
	u32 max_ord, max_ird;

	u32 id;
	struct ocrdma_pd *pd;

	enum ib_qp_type qp_type;

	int sgid_idx;
	u32 qkey;
	bool dpp_enabled;
	u8 *ird_q_va;
	bool signaled;
};

struct ocrdma_ucontext {
	struct ib_ucontext ibucontext;

	struct list_head mm_head;
	struct mutex mm_list_lock; /* protects list entries of mm type */
	struct ocrdma_pd *cntxt_pd;
	int pd_in_use;

	struct {
		u32 *va;
		dma_addr_t pa;
		u32 len;
	} ah_tbl;
};

struct ocrdma_mm {
	struct {
		u64 phy_addr;
		unsigned long len;
	} key;
	struct list_head entry;
};

static inline struct ocrdma_dev *get_ocrdma_dev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct ocrdma_dev, ibdev);
}

static inline struct ocrdma_ucontext *get_ocrdma_ucontext(struct ib_ucontext
							  *ibucontext)
{
	return container_of(ibucontext, struct ocrdma_ucontext, ibucontext);
}

static inline struct ocrdma_pd *get_ocrdma_pd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct ocrdma_pd, ibpd);
}

static inline struct ocrdma_cq *get_ocrdma_cq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct ocrdma_cq, ibcq);
}

static inline struct ocrdma_qp *get_ocrdma_qp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct ocrdma_qp, ibqp);
}

static inline struct ocrdma_mr *get_ocrdma_mr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct ocrdma_mr, ibmr);
}

static inline struct ocrdma_ah *get_ocrdma_ah(struct ib_ah *ibah)
{
	return container_of(ibah, struct ocrdma_ah, ibah);
}

static inline struct ocrdma_srq *get_ocrdma_srq(struct ib_srq *ibsrq)
{
	return container_of(ibsrq, struct ocrdma_srq, ibsrq);
}

static inline int is_cqe_valid(struct ocrdma_cq *cq, struct ocrdma_cqe *cqe)
{
	int cqe_valid;
	cqe_valid = le32_to_cpu(cqe->flags_status_srcqpn) & OCRDMA_CQE_VALID;
	return (cqe_valid == cq->phase);
}

static inline int is_cqe_for_sq(struct ocrdma_cqe *cqe)
{
	return (le32_to_cpu(cqe->flags_status_srcqpn) &
		OCRDMA_CQE_QTYPE) ? 0 : 1;
}

static inline int is_cqe_invalidated(struct ocrdma_cqe *cqe)
{
	return (le32_to_cpu(cqe->flags_status_srcqpn) &
		OCRDMA_CQE_INVALIDATE) ? 1 : 0;
}

static inline int is_cqe_imm(struct ocrdma_cqe *cqe)
{
	return (le32_to_cpu(cqe->flags_status_srcqpn) &
		OCRDMA_CQE_IMM) ? 1 : 0;
}

static inline int is_cqe_wr_imm(struct ocrdma_cqe *cqe)
{
	return (le32_to_cpu(cqe->flags_status_srcqpn) &
		OCRDMA_CQE_WRITE_IMM) ? 1 : 0;
}

static inline int ocrdma_resolve_dmac(struct ocrdma_dev *dev,
		struct ib_ah_attr *ah_attr, u8 *mac_addr)
{
	struct in6_addr in6;

	memcpy(&in6, ah_attr->grh.dgid.raw, sizeof(in6));
	if (rdma_is_multicast_addr(&in6))
		rdma_get_mcast_mac(&in6, mac_addr);
	else if (rdma_link_local_addr(&in6))
		rdma_get_ll_mac(&in6, mac_addr);
	else
		memcpy(mac_addr, ah_attr->dmac, ETH_ALEN);
	return 0;
}

static inline char *hca_name(struct ocrdma_dev *dev)
{
	switch (dev->nic_info.pdev->device) {
	case OC_SKH_DEVICE_PF:
	case OC_SKH_DEVICE_VF:
		return OC_NAME_SH;
	default:
		return OC_NAME_UNKNOWN;
	}
}

static inline int ocrdma_get_eq_table_index(struct ocrdma_dev *dev,
		int eqid)
{
	int indx;

	for (indx = 0; indx < dev->eq_cnt; indx++) {
		if (dev->eq_tbl[indx].q.id == eqid)
			return indx;
	}

	return -EINVAL;
}

static inline u8 ocrdma_get_asic_type(struct ocrdma_dev *dev)
{
	if (dev->nic_info.dev_family == 0xF && !dev->asic_id) {
		pci_read_config_dword(
			dev->nic_info.pdev,
			OCRDMA_SLI_ASIC_ID_OFFSET, &dev->asic_id);
	}

	return (dev->asic_id & OCRDMA_SLI_ASIC_GEN_NUM_MASK) >>
				OCRDMA_SLI_ASIC_GEN_NUM_SHIFT;
}

static inline u8 ocrdma_get_pfc_prio(u8 *pfc, u8 prio)
{
	return *(pfc + prio);
}

static inline u8 ocrdma_get_app_prio(u8 *app_prio, u8 prio)
{
	return *(app_prio + prio);
}

static inline u8 ocrdma_is_enabled_and_synced(u32 state)
{	/* May also be used to interpret TC-state, QCN-state
	 * Appl-state and Logical-link-state in future.
	 */
	return (state & OCRDMA_STATE_FLAG_ENABLED) &&
		(state & OCRDMA_STATE_FLAG_SYNC);
}

#endif
